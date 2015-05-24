#simh support/migration tools.

The tools are organized into categories, each of which has a subdirectory.

For consistency, each tool has its own subdirectory, even if it only contains a single file.
(READMEs for tools without them are welcome)


## Configuration support

Tools to assist with configuring a simulator.

Directory | Contents
---- | ----
config11 | Calculate the floating address space layout of a PDP-11 or VAX.

## Converters

Tools that convert/support simulator data file formats

Directory | Contents
---- | ----
asc | Convert ASCII file line endings
decsys | Convert decimal listing file to a DECtape file
dtos8cvt | Convert a PDP-8 DECtape image from OS/8 format to simulator format.
gt7cvt | Convert a gt7 magtape dump to a SIMH magtape
hpconvert | Convert an HP disc image between SIMH and HPDrive formats
indent | Convert simulator sources to 4-column tabs
littcvt | Remove density maker from litt format tapes
m8376 | Assembles 8 PROM files into a 32bit binary file
mt2tpc | Convert a simh simulated magtape to TPC format
mtcvtfix | Fix a SIMH magtape containing a misread EOF
mtcvtodd | Convert an E11 magtape (with odd record sizes) to SIMH
mtcvtv23 | Convert a tape image in .TPC format to SIMH (.tap)
noff | Remove <ff> (formfeed, \f) from a source listing
sfmtcvt | Convert a Motorola S format PROM dump to a binary file
strrem | Remove a string from each line of a file
strsub | Substitute a string in each line of a file
tar2mt | Convert a tar file to a simulated 8192B blocked magtape
tp512cvt | Convert a tp data file to a simulated 512B blocked magtape
tpc2mt | Convert a TPC simulated magtape to simh format

## Cross-assemblers

Cross-assemblers for various machine architectures

Directory | Contents
---- | ----
hpasm | Assembler for the HP2100
macro1 | Assembler for the PDP-1
macro7 | Assembler for the PDP-7
macro8x | Assembler for the PDP-8
macro11 | Assembler for the PDP-11

## Extracters

Data extraction tools

Except as noted, all read SIMH tape container format.

Directory | Contents
---- | ----
backup | Extract files from a TOPS-10 backup tape 
ckabstape | Disassemble 18-bit binary paper tape
mmdir | List directory of Interdata MDM tape
mtdump | Dump the record structure of a SIMH, E11, TPC, or P7B
ods2 | Directory, Copy & Search commands for VMS ODS2 disk images
rawcopy | Create SIMH disk image from physical media on RAW device.
sdsdump | Disassemble SDS SDS paper tape
tpdump | Dump files on IBM 1401 tape

## File systems

Provide access to a foreign files system from a host machine.

Directory | Contents
---- | ----
putr | Read (and write some) DEC filesystems from PCs

