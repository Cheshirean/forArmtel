#ifndef _EFI_SPI_H_
#define _EFI_SPI_H_

// Define the SPI protocol GUID

#define EFI_SPI_PROTOCOL_GUID \
	{ 0x1a660d9, 0x8009, 0x4330, \
	{ 0xba, 0x89, 0x71, 0xb0, 0x76, 0xcd, 0x5d, 0xa }}

//
// Forward reference for ANSI C compatibility
//
typedef struct _EFI_SPI_PROTOCOL  EFI_SPI_PROTOCOL;

//
// SPI protocol data structures and definitions
//
//
// Number of Prefix Opcodes allowed on the SPI interface
//
#define SPI_NUM_PREFIX_OPCODE 2

//
// Number of Opcodes in the Opcode Menu
//
#define SPI_NUM_OPCODE  8

//
// Opcode Type
//    EnumSpiOpcodeCommand: Command without address
//    EnumSpiOpcodeRead: Read with address
//    EnumSpiOpcodeWrite: Write with address
//
typedef enum {
  EnumSpiOpcodeReadNoAddr,
  EnumSpiOpcodeWriteNoAddr,
  EnumSpiOpcodeRead,
  EnumSpiOpcodeWrite,
  EnumSpiOpcodeMax
} SPI_OPCODE_TYPE;

typedef enum {
  EnumSpiCycle20MHz,
  EnumSpiCycle33MHz,
  EnumSpiCycle66MHz,  // not supported by PCH
  EnumSpiCycle50MHz,
  EnumSpiCycleMax
} SPI_CYCLE_FREQUENCY;

typedef enum {
  EnumSpiRegionAll,
  EnumSpiRegionBios,
  EnumSpiRegionMe,
  EnumSpiRegionGbE,
  EnumSpiRegionDescriptor,
  EnumSpiRegionPlatformData,
  EnumSpiRegionMax
} SPI_REGION_TYPE;

//
// Hardware Sequencing required operations (as listed in CougarPoint EDS Table 5-55: "Hardware
// Sequencing Commands and Opcode Requirements"
//
typedef enum {
  EnumSpiOperationWriteStatus,
  EnumSpiOperationProgramData_1_Byte,
  EnumSpiOperationProgramData_64_Byte,
  EnumSpiOperationReadData,
  EnumSpiOperationWriteDisable,
  EnumSpiOperationReadStatus,
  EnumSpiOperationWriteEnable,
  EnumSpiOperationFastRead,
  EnumSpiOperationEnableWriteStatus,
  EnumSpiOperationErase_256_Byte,
  EnumSpiOperationErase_4K_Byte,
  EnumSpiOperationErase_8K_Byte,
  EnumSpiOperationErase_64K_Byte,
  EnumSpiOperationFullChipErase,
  EnumSpiOperationJedecId,
  EnumSpiOperationDualOutputFastRead,
  EnumSpiOperationDiscoveryParameters,
  EnumSpiOperationOther,
  EnumSpiOperationMax
} SPI_OPERATION;

//
// SPI Command Configuration
//   Frequency       The expected frequency to be used (value to be programmed to the SSFC
//                   Register)
//   Operation       Which Hardware Sequencing required operation this opcode respoinds to.
//                   The required operations are listed in EDS Table 5-55: "Hardware
//                   Sequencing Commands and Opcode Requirements"
//                   If the opcode does not corresponds to any operation listed, use
//                   EnumSpiOperationOther, and provides TYPE and Code for it in
//                   SpecialOpcodeEntry.
//
typedef struct _SPI_COMMAND_CONFIG {
  SPI_CYCLE_FREQUENCY Frequency;
  SPI_OPERATION       Operation;
} SPI_COMMAND_CONFIG;

//
// Special Opcode entries
//   OpcodeIndex     Opcode Menu Index whose Opcode Type/Menu Configuration Register need to be
//                   overrided or programmed per "Type" and "Code". Filled this field with 0xFF
//                   as the end tag of SpecialOpcodeEntry.
//   Type            Operation Type (value to be programmed to the OPTYPE register)
//   Code            The opcode (value to be programmed to the OPMENU register)
//
typedef struct _SPI_SPECIAL_OPCODE_ENTRY {
  UINT8           OpcodeIndex;
  SPI_OPCODE_TYPE Type;
  UINT8           Code;
} SPI_SPECIAL_OPCODE_ENTRY;

//
// Initialization data that can be used to identify SPI flash part
//    DeviceId0       Device ID0 of the SPI device
//    DeviceId1       Device ID1 of the SPI device
//    BiosStartOffset The offset of the start of the BIOS image relative to the flash device.
//                    Please note this is a Flash Linear Address, NOT a memory space address.
//                    This value is platform specific and depends on the system flash map.
//                    This value is only used on non Descriptor mode.
//
typedef struct _SPI_FLASH_DATA {
  UINT8 DeviceId0;
  UINT8 DeviceId1;
  UINTN BiosStartOffset;  // (Flash part Size - Bios Size)
} SPI_TYPE_DATA;

//
// Initialization data table loaded to the SPI host controller
//    VendorId            Vendor ID of the SPI device
//    TypeDataNum         The number of TypeData
//    TypeData            The initialization data that can be used to identify SPI flash part
//    PrefixOpcode        Prefix opcodes which are loaded into the SPI host controller
//    SpiCmdConfig        Determines Opcode Type, Menu and Frequency of the SPI commands
//    SpecialOpcodeEntry  Special Opcode entry for the special operations.
//    BiosSize            The the BIOS Image size in flash. This value is platform specific
//                        and depends on the system flash map. Please note BIOS Image size may
//                        be smaller than BIOS Region size (in Descriptor Mode) or the flash size
//                        (in Non Descriptor Mode), and in this case, BIOS Image is supposed to be
//                        placed at the top end of the BIOS Region (in Descriptor Mode) or the flash
//                        (in Non Descriptor Mode)
//
// Note:  Most of time, the SPI flash parts with the same vendor would have the same
//        Prefix Opcode, Opcode menu, so you can provide one table for the SPI flash parts with
//        the same vendor.
//
typedef struct _SPI_INIT_DATA {
  UINT8                     VendorId;
  UINT8                     TypeDataNum;
  SPI_TYPE_DATA             *TypeData;
  UINT8                     PrefixOpcode[SPI_NUM_PREFIX_OPCODE];
  SPI_COMMAND_CONFIG        SpiCmdConfig[SPI_NUM_OPCODE];
  SPI_SPECIAL_OPCODE_ENTRY  *SpecialOpcodeEntry;
  UINTN                     BiosSize;
} SPI_INIT_DATA;

//
// Protocol member functions
//
typedef
EFI_STATUS
(EFIAPI *EFI_SPI_INIT) (
  IN EFI_SPI_PROTOCOL     * This,
  IN SPI_INIT_DATA        * InitData
  );

/*++

Routine Description:
  
  Initializes the host controller to execute SPI commands.
    
Arguments:

  This                    Pointer to the EFI_SPI_PROTOCOL instance.
  InitData                Pointer to caller-allocated buffer containing the SPI 
                          interface initialization table.
                          
Returns:

  EFI_SUCCESS             Opcode initialization on the SPI host controller completed.
  EFI_ACCESS_DENIED       The SPI configuration interface is locked.
  EFI_OUT_OF_RESOURCES    Not enough resource available to initialize the device.
  EFI_DEVICE_ERROR        Device error, operation failed.
    
--*/
typedef
EFI_STATUS
(EFIAPI *EFI_SPI_LOCK) (
  IN EFI_SPI_PROTOCOL     * This
  );

/*++

Routine Description:
  
  Lock the SPI Static Configuration Interface. 
  Once locked, the interface is no longer open for configuration changes.
  The lock state automatically clears on next system reset.
    
Arguments:

  This      Pointer to the EFI_SPI_PROTOCOL instance.
                          
Returns:

  EFI_SUCCESS             Lock operation succeed.
  EFI_DEVICE_ERROR        Device error, operation failed.
  EFI_ACCESS_DENIED       The interface has already been locked.
    
--*/
typedef
EFI_STATUS
(EFIAPI *EFI_SPI_EXECUTE) (
  IN     EFI_SPI_PROTOCOL   * This,
  IN     UINT8              OpcodeIndex,
  IN     UINT8              PrefixOpcodeIndex,
  IN     BOOLEAN            DataCycle,
  IN     BOOLEAN            Atomic,
  IN     BOOLEAN            ShiftOut,
  IN     UINTN              Address,
  IN     UINT32             DataByteCount,
  IN OUT UINT8              *Buffer,
  IN     SPI_REGION_TYPE    SpiRegionType
  );

/*++

Routine Description:
  
  Execute SPI commands from the host controller.
  
Arguments:

  This                    Pointer to the EFI_SPI_PROTOCOL instance.
  OpcodeIndex             Index of the command in the OpCode Menu.
  PrefixOpcodeIndex       Index of the first command to run when in an atomic cycle sequence.
  DataCycle               TRUE if the SPI cycle contains data
  Atomic                  TRUE if the SPI cycle is atomic and interleave cycles are not allowed.
  ShiftOut                If DataByteCount is not zero, TRUE to shift data out and FALSE to shift data in.
  Address                 In Descriptor Mode, for Descriptor Region, GbE Region, ME Region and Platform 
                          Region, this value specifies the offset from the Region Base; for BIOS Region, 
                          this value specifies the offset from the start of the BIOS Image. In Non 
                          Descriptor Mode, this value specifies the offset from the start of the BIOS Image. 
                          Please note BIOS Image size may be smaller than BIOS Region size (in Descriptor 
                          Mode) or the flash size (in Non Descriptor Mode), and in this case, BIOS Image is 
                          supposed to be placed at the top end of the BIOS Region (in Descriptor Mode) or 
                          the flash (in Non Descriptor Mode)
  DataByteCount           Number of bytes in the data portion of the SPI cycle.
  Buffer                  Pointer to caller-allocated buffer containing the dada received or sent during the SPI cycle.
  SpiRegionType           SPI Region type. Values EnumSpiRegionBios, EnumSpiRegionGbE, EnumSpiRegionMe, 
                          EnumSpiRegionDescriptor, and EnumSpiRegionPlatformData are only applicable in 
                          Descriptor mode. Value EnumSpiRegionAll is applicable to both Descriptor Mode
                          and Non Descriptor Mode, which indicates "SpiRegionOffset" is actually relative
                          to base of the 1st flash device (i.e., it is a Flash Linear Address).

Returns:

  EFI_SUCCESS             Command succeed.
  EFI_INVALID_PARAMETER   The parameters specified are not valid.
  EFI_UNSUPPORTED         Command not supported.
  EFI_DEVICE_ERROR        Device error, command aborts abnormally.
  
--*/

//
// Protocol definition
//
struct _EFI_SPI_PROTOCOL {
  EFI_SPI_INIT    Init;
  EFI_SPI_EXECUTE Execute;
};

#endif
