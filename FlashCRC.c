#include <Uefi.h>
#include <Uefi/UefiSpec.h>
#include <Library/IoLib.h>
#include <Library/PciLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>

#include "Spi.h"

#define MEI_H_CB_WW   0x00
#define MEI_H_CSR     0x04
#define MEI_ME_CB_RW  0x08
#define MEI_ME_CSR_HA 0x0c

#define BIOS_SIZE 0x800000

/* Spi Flash offsets */
#define FLASH_DESCRIPTOR_BASE 0x0
#define FLASH_DESCRIPTOR_SIZE 0x1000
#define GBE_REGION_BASE       0x1000
#define GBE_REGION_SIZE       0x2000
#define ME_REGION_BASE        0x3000
#define ME_REGION_SIZE        0x57D000
#define BIOS_REGION_BASE      0x580000
#define BIOS_REGION_SIZE      0x280000
#define NVRAM_REGION_BASE     0x580000
#define NVRAM_REGION_SIZE     0x40000

/* Skip offset for reading from SPI flash */
#define GBE_REGION_SKIP_BASE   0x1000
#define GBE_REGION_SKIP_SIZE   0x2000
#define ME_REGION_SKIP_BASE    0x33c0
#define ME_REGION_SKIP_SIZE    0xe2c40
#define NVRAM_REGION_SKIP_BASE 0x580000
#define NVRAM_REGION_SKIP_SIZE 0x40000

#define CRC32_BASE 0x2ffc
#define CRC32_SIZE 0x4

#define WRITE_PROTECT_EN 0x80000000
#define READ_PROTECT_EN  0x8000

/* LPC device number */
#define PCI_BUS_NUMBER_PCH          0
#define PCI_DEVICE_NUMBER_PCH_LPC   31
#define PCI_FUNCTION_NUMBER_PCH_LPC 0

/* LPC offsets */
#define SPI_CONTROLLER_OFFSET 0x3800

/* LPC registers */
#define PCH_LPC_RCBA 0xf0

struct MeiCsr {
	UINT32 interrupt_enable: 1;
	UINT32 interrupt_status: 1;
	UINT32 interrupt_generate: 1;
	UINT32 ready: 1;
	UINT32 reset: 1;
	UINT32 reserved: 3;
	UINT32 buffer_read_ptr: 8;
	UINT32 buffer_write_ptr: 8;
	UINT32 buffer_depth: 8;
} __attribute__ ((packed));

struct MeiHeader {
	UINT32 client_address: 8;
	UINT32 host_address: 8;
	UINT32 length: 9;
	UINT32 reserved: 6;
	UINT32 is_complete: 1;
} __attribute__ ((packed));

struct MkhiHeader {
	UINT32 group_id: 8;
	UINT32 command: 7;
	UINT32 is_response: 1;
	UINT32 reserved: 8;
	UINT32 result: 8;
} __attribute__ ((packed));

UINT32 HeciMBar;

EFI_GUID gPeiSpiPpiGuid = EFI_SPI_PROTOCOL_GUID;

VOID HeciRead(UINT32 Reg, UINT32 *Data)
{
	*Data = MmioRead32(HeciMBar + Reg);
}

VOID HeciWrite(UINT32 Reg, UINT32 *Data)
{
	MmioWrite32(HeciMBar + Reg, *Data);
}

static EFI_STATUS mei_wait_for_me_ready(void)
{
	struct MeiCsr me;
	UINT32 try = 1000000;

	while (try--) {
		HeciRead(MEI_H_CSR, (UINT32 *)&me);
		if (me.ready)
			return EFI_SUCCESS;
	}

	return 1;
}

VOID mei_reset(VOID)
{
	struct MeiCsr host;

	if (mei_wait_for_me_ready() != EFI_SUCCESS)
		return;

	/* Reset host and ME circular buffers for next message */
	HeciRead(MEI_H_CSR, (UINT32 *)&host);
	host.reset = 1;
	host.interrupt_generate = 1;
	HeciWrite(MEI_H_CSR, (UINT32 *)&host);

	if (mei_wait_for_me_ready() != EFI_SUCCESS)
		return;

	/* Re-init and indicate host is ready */
	HeciRead(MEI_H_CSR, (UINT32 *)&host);
	host.interrupt_generate = 1;
	host.ready = 1;
	host.reset = 0;
	HeciWrite(MEI_H_CSR, (UINT32 *)&host);
}

static EFI_STATUS MeiSendMsg(struct MeiHeader *Mei, struct MkhiHeader *Mkhi, VOID *ReqData)
{
	struct MeiCsr Host;
	UINT32 nData, n;
	UINT32 *data;

	/* Number of dwords to write, ignoring MKHI */
	nData = (Mei->length) >> 2;

	/* Pad non-dword aligned request message length */
	if (Mei->length & 3)
		nData++;
	if (!nData) {
		return 1;
	}
	nData++; /* Add MEI header */

	/*
	 * Make sure there is still room left in the circular buffer.
	 * Reset the buffer pointers if the requested message will not fit.
	 */
	HeciRead(MEI_H_CSR, (UINT32 *)&Host);
	if ((Host.buffer_depth - Host.buffer_write_ptr) < nData) {
		mei_reset();
		HeciRead(MEI_H_CSR, (UINT32 *)&Host);
	}

	/*
 	 * This implementation does not handle splitting large messages
	 * across multiple transactions.  Ensure the requested length
	 * will fit in the available circular buffer depth.
	 */
	if ((Host.buffer_depth - Host.buffer_write_ptr) < nData) {
		return 1;
	}

	/* Write MEI header */
	HeciWrite(MEI_H_CB_WW, (UINT32 *)&Mei);
	nData--;

	/* Write MKHI header */
	HeciWrite(MEI_H_CB_WW, (UINT32 *)&Mkhi);
	nData--;

	/* Write message data */
	data = ReqData;
	for (n = 0; n < nData; n++)
		HeciWrite(MEI_H_CB_WW, (UINT32 *)&data[n]);


	/* Generate interrupt to the ME */
	HeciRead(MEI_H_CSR, (UINT32 *)&Host);
	Host.interrupt_generate = 1;
	HeciWrite(MEI_H_CSR, (UINT32 *)&Host);

	/* Make sure ME is ready after sending request data */
	return mei_wait_for_me_ready();
}

EFI_STATUS
EFIAPI
FlashCRCEntry(IN EFI_HANDLE       ImageHandle,
              IN EFI_SYSTEM_TABLE *SystemTable)
{
	UINT32 SpiControllerAddr;
	EFI_STATUS Status;
	UINT32 CalcCrc32;
	UINT32 RefCrc32;
	UINT8 *Buffer;
	UINTN HandleCount;
	EFI_HANDLE *HandleBuffer;
	UINT32 i;
	EFI_SPI_PROTOCOL *Spi;

	/* for HECI */
	struct Host;
	struct MkhiHeader Mkhi = {
		.group_id = 3,
		.command = 3,
		.is_response = 0,
	};
	struct MeiHeader Mei = {
		.is_complete = 1,
		.client_address = 0x7,
		.host_address = 0x0,
		.length = sizeof(Mkhi) + sizeof(UINT32),
	};
	UINT32 Data;
	
	/* Read BIOS CRC32 from SPI flash */
	Status = gBS->LocateHandleBuffer(ByProtocol, &gPeiSpiPpiGuid, NULL, &HandleCount, &HandleBuffer);
	if (!EFI_ERROR(Status)) {
		for (i = 0; i < HandleCount; i++) { 
	 		Status = gBS->HandleProtocol(HandleBuffer[i], &gPeiSpiPpiGuid, (VOID **)&Spi);
	 		if (!EFI_ERROR(Status)) {
				RefCrc32 = 0;
				Status = Spi->Execute(Spi,
	 								  1,
	 								  0,
	 								  TRUE,
	 								  TRUE,
	 								  FALSE,
	 								  (UINTN) 0x2ffc,
	 								  sizeof (RefCrc32),
	 								  (UINT8 *) &RefCrc32,
	 								  4);
	 		}
		}
	}

	/* Read BIOS from mmio and calculate his CRC32 */
	SystemTable->ConOut->OutputString(SystemTable->ConOut, L"\n\r");
	SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Calculate BIOS CRC...\n\r");

	Buffer = AllocateRuntimeZeroPool(BIOS_SIZE);
	
	for (i = 0; i < GBE_REGION_SKIP_BASE; i+=256) {
	  	Status = Spi->Execute(Spi,
	  						  1,
	  						  0,
	  						  TRUE,
	  						  TRUE,
	  						  FALSE,
	  						  (UINTN) i,
	  						  256,
	  						  (UINT8 *) &Buffer[i],
	  						  4);
	}

	for (i = GBE_REGION_SKIP_BASE + GBE_REGION_SKIP_SIZE;
	 	 i < ME_REGION_SKIP_BASE; i+=64) {
	  	Status = Spi->Execute(Spi,
	  						  1,
	  						  0,
	  						  TRUE,
	  						  TRUE,
	  						  FALSE,
	  						  (UINTN) i,
	  						  64,
	  						  (UINT8 *) &Buffer[i],
	  						  4);
	}

	for (i = ME_REGION_SKIP_BASE + ME_REGION_SKIP_SIZE;
		 i < NVRAM_REGION_SKIP_BASE; i+=256) {
	  	Status = Spi->Execute(Spi,
	  						  1,
	  						  0,
	  						  TRUE,
	 						  TRUE,
	  						  FALSE,
	  						  (UINTN) i,
	  						  256,
	  						  (UINT8 *) &Buffer[i],
	  						  4);
	}

	for (i = NVRAM_REGION_SKIP_BASE + NVRAM_REGION_SKIP_SIZE;
	 	 i < BIOS_SIZE; i+=256) {
	  	Status = Spi->Execute(Spi,
	  						  1,
	  						  0,
	  						  TRUE,
	  						  TRUE,
	  						  FALSE,
	  						  (UINTN) i,
	  						  256,
	  						  (UINT8 *) &Buffer[i],
	  						  4);
	}

	gBS->CalculateCrc32((VOID *)Buffer, (UINTN)(BIOS_SIZE), &CalcCrc32);

	gBS->FreePool(Buffer);
	gBS->FreePool(HandleBuffer);

	Data = 6;

	HeciMBar = PciRead32(PCI_LIB_ADDRESS(0, 22, 0, 0) + 0x10) & 0xfffffff0;

	MeiSendMsg(&Mei, &Mkhi, &Data);

	SpiControllerAddr = PciRead32(PCI_LIB_ADDRESS(PCI_BUS_NUMBER_PCH,
												  PCI_DEVICE_NUMBER_PCH_LPC,
												  PCI_FUNCTION_NUMBER_PCH_LPC,
												  PCH_LPC_RCBA)) + SPI_CONTROLLER_OFFSET - 1;

	MmioWrite32(SpiControllerAddr + 0x74,
				(WRITE_PROTECT_EN |
				 (((NVRAM_REGION_BASE - 1) >> 12) << 16)));
	MmioWrite32(SpiControllerAddr + 0x78,
				(WRITE_PROTECT_EN |
				 ((NVRAM_REGION_BASE + NVRAM_REGION_SIZE) >> 12) |
				 0x7ff0000));
	MmioWrite16(SpiControllerAddr + 0x4, (MmioRead16(SpiControllerAddr + 0x4) | READ_PROTECT_EN));

	if (RefCrc32 != CalcCrc32) {
		Print(L"Bad CRC (0x%08x)! System halted!\n\r", CalcCrc32);
		asm("cli");
		while(1){}
	} else {
		Print(L"CRC is good (0x%08x)! Loading...\n\r", CalcCrc32);
	}

	return EFI_SUCCESS;
}
