# PiFMX
PiFMX - this is FM transmitter for **Raspberry Pi**.  
PiFMX - will support (hope) **full RDS functions**.   
PIFMX - works on the latest **Raspberry Pi OS** system and on the board **Raspberry Pi 4B**.  
Original repository: [PiFmRds](github.com/ChristopheJacquet/PiFmRds)  

# Functions RDS
* **PI** - Programme Identification (4 characters: `XXXX`)  
* **PTY** - Programme Type (`00 - 31`)
* **PS** - Programme Service Name (8 characters: `XXXXXXXX`)  
* **RT** - Radio Text (`64 characters`)
* **RT(A/B)** - Radio Text (A/B Switches). Modes: only A, only B, AB. Example: `A/B/AB`  
* **RT+** - Radio Text+ (Tags: `00 - 63`) (Symbols: `00 - 64`). Example (tags.first symbol.last symbol): `XX.XX.XX,XX.XX.XX`
* **TP** - Traffic Programme identification (`0/1`)   
* **TA** - Traffic Announcement identification (`0/1`) 
* **AF(A)** - Alternative Frequencies List (A method). Example: `87.6 87.8 91.1`  
* **AF(B)** - Alternative Frequencies List (B method). Example (main|same|regional): `87.6,90.1 95.5,90.5 90.6`  
* **M/S** - Music Speech switch (M/S). Example: `M/S`
* **ECC** - Extended Country Code (2 characters: `XX`)  
* **LIC** - Language Identification Code (2 characters: `XX`)  
* **PIN** - Programme Item Number. (Date: `01-31`, Hours: `00-23`, Minutes: `00-59`). Example (date|hours|minutes): `XX,XX,XX`  
* **PTYN** - Programme Type Name. (8 characters: `XXXXXXXX`)  
* **Long PS** - Long Programme Service Name (`32 characters`)  
* **DI(A,C,D)** - Decoder Identification (Stereo, Artifical Head, Compressed, Dynamic PTY). Example: `S/SA/SD/SC/A/AC/AD/C/CA/CD/D/ACD/ACDS`  
* **EON** - Enhanced Other Networks Information. (PI,PS,AF,MF,LI,PTY,TP,TA,PIN). Example: `D392,WDR 2   ,102.1 88.5 90.5,87.6 92.1,0000,10,ON,OFF,022254`  
* **CT** - Clock Time. Changing the time zone. Example: `-1, +3, +9:30`  

# Development Statuses (RDS functions) (global and rds_ctl)
**PI** (`-pi`) **GLOBAL** - ✅ realized  
**PI** (`PI`) **RDS_CTL** - ✅ realized 

**PTY** (`-pty`) **GLOBAL** - ✅ realized  
**PTY** (`PTY`) **RDS_CTL** - ✅ realized  

**PS** (`-ps`) **GLOBAL** - ✅ realized  
**PS** (`PS`) **RDS_CTL** - ✅ realized 

**RT** (`-rt`) **GLOBAL** - ✅ realized  
**RT** (`RT`) **RDS_CTL** - ✅ realized  

**RT(A/B)** (`-rts`) **GLOBAL** - ❌ not realized  
**RT(A/B)** (`RTS`) **RDS_CTL** - ❌ not realized

**RT+** (`-rtp`) **GLOBAL** - ❌ not realized  
**RT+** (`RTP`) **RDS_CTL** - ❌ not realized  

**TP** (`-tp`) **GLOBAL** - ✅ realized  
**TP** (`TP`) **RDS_CTL** - ✅ realized  

**TA** (`-ta`) **GLOBAL** - ✅ realized  
**TA** (`TA`) **RDS_CTL** - ✅ realized  

**AF(A)** (`-afa`) **GLOBAL** - ❌ not realized   
**AF(A)** (`AFA`) **RDS_CTL** - ❌ not realized  

**AF(B)** (`-afb`) **GLOBAL** - ❌ not realized   
**AF(B)** (`AFB`) **RDS_CTL** - ❌ not realized   

**M/S** (`-ms`) **GLOBAL** - ✅ realized   
**M/S** (`MS`) **RDS_CTL** - ✅ realized   

**ECC** (`-ecc`) **GLOBAL** - ✅ realized  
**ECC** (`ECC`) **RDS_CTL** - ✅ realized 

**LIC** (`-lic`) **GLOBAL** - ✅ realized  
**LIC** (`LIC`) **RDS_CTL** - ✅ realized  

**PIN** (`-pin`) **GLOBAL** - ✅ realized   
**PIN** (`PIN`) **RDS_CTL** - ✅ realized   

**PTYN** (`-ptyn`) **GLOBAL** - ❌ not realized   
**PTYN** (`PTYN`) **RDS_CTL** - ❌ not realized   

**Long PS** (`-lps`) **GLOBAL** - ❌ not realized   
**Long PS** (`LPS`) **RDS_CTL** - ❌ not realized   

**DI(S,A,C,D)** (`-di`) **GLOBAL** - ✅ realized   
**DI(S,A,C,D)** (`DI`) **RDS_CTL** - ✅ realized   

**EON** (`-eon`) **GLOBAL** - ❌ not realized  
**EON** (`EON`) **RDS_CTL** - ❌ not realized  

**CT** (`-ct`) **GLOBAL** - ❌ not realized    
**CT** (`CT`) **RDS_CTL** - ❌ not realized   

# Development Statuses (Global functions)
**MONO** (`-mn`) - Conclusion in mono sound - ❌ not realized  
**FREQUENCY** (`-freq`) - increase to **64 MHz** - ❌ not realized  
**RDS POWER** (`-rdsp`) - RDS signal level - ❌ not realized  
**FM POWER** (`-fmp`) - Power issued by Raspberry Pi for FM - ❌ not realized  
**CHANGE GPIO** (`-gpio`) - Change of Pin output for the antenna - ❌ not realized  

# Installation 
For continuous operation of the FM transmitter on the Raspberry Pi 4B, the following command is entered:  
```bash
echo "performance"| sudo tee /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
```

Module installation (before installing PiFMX):
```
sudo apt install libsndfile1-dev
```

Installation PiFMX:  
```bash
git clone https://github.com/KOTYA8/PiFMX.git
cd PiFMX/src
make clean
make
```

PiFMX launch:  
```
sudo ./pi_fm_x
```

# General Arguments
By default the PS changes back and forth between `RPi-Live` and a sequence number, starting at `00000000`. The PS changes around one time per second.  
```bash
sudo ./pi_fm_x [-freq freq] [-audio file] [-ppm ppm_error] [-ctl] [-pi pi_code] [-ps ps_text] [-rt rt_text] [-ecc code] [-lic code] [-pty code] [-tp 0/1] [-ta 0/1] [-ms M/S] [-di S/SA/SD/SC/A/AC/AD/C/CA/CD/D/ACD,ACDS] [-pin XX,XX,XX]
```
All arguments are optional:  

**Global:**  
  
* `-freq` specifies the carrier frequency (76 - 108 MHz). Example: `-freq 107.9`.  
* `-audio` specifies an audio file to play as audio. The sample rate does not matter: PiFMX will resample and filter it. If a stereo file is provided, Pi-FM-RDS will produce an FM-Stereo signal. Example: `-audio sound.wav`. The supported formats depend on libsndfile. This includes WAV and Ogg/Vorbis (among others) but not MP3. Specify - as the file name to read audio data on standard input (useful for piping audio into Pi-FM-RDS, see below).
* `-ppm` specifies your Raspberry Pi's oscillator error in parts per million (ppm), see below.  
   
**Control RDS (remotely):**  
   
* `-ctl` specifies a named pipe (FIFO) to use as a control channel to change PS and RT at run-time (see below).
  
**RDS:**  
  
* `-pi` specifies the PI-code of the RDS broadcast. 4 hexadecimal digits. Example: `-pi FFFF`.  
* `-ps` specifies the station name (Program Service name, PS) of the RDS broadcast. Limit: 8 characters. Example: `-ps RASP-PI`.  
* `-rt` specifies the radiotext (RT) to be transmitted. Limit: 64 characters. Example: `-rt 'Hello, world!'`.  
* `-ecc` specifies the country for the transmitter (Extended Country Code, ECC). Displayed through 2 characters, example: `-ecc E0`.  
* `-lic` specifies the language speaking at the radio station (Language Identification Code, LIC). Displayed through 2 characters, example: `-lic 20`.  
* `-pty` specifies the type of program for radio stations (Programme Type, PTY). Displayed through 1 or 2 characters, example: `-pty 10`.  
* `-tp` specifies the availability of radio stations, transport communication (Traffic Programme identification, TP). Displayed through 1 characters, example: `-tp 1`.  
* `-ta` specifies the start of the transport message (Traffic Announcement identification, TA). Displayed through 1 characters, example: `-ta 1`.  
* `-ms` specifies the program or music for the radio station (Music Speech switch, M/S). Displayed through 1 characters, example: `-ms M`.  
* `-di` specifies the flags of the supported radio stations (Decoder Identification, (Stereo, Artifical Head, Compressed, Dynamic PTY)). Displayed through 1 or 4 characters, example: `-di SACD`.
* `-pin` specifies the identification of the program at the radio station (Programme Item Number. (Date: 01-31, Hours: 00-23, Minutes: 00-59)). Displayed through 5 or 8 characters, example: `-pin 1,12,22`.  

### Clock calibration (only if experiencing difficulties)

The RDS standards states that the error for the 57 kHz subcarrier must be less than ± 6 Hz, i.e. less than 105 ppm (parts per million). The Raspberry Pi's oscillator error may be above this figure. That is where the `-ppm` parameter comes into play: you specify your Pi's error and PiFMX adjusts the clock dividers accordingly.

In practice, I found that Pi-FM-RDS works okay even without using the `-ppm` parameter. I suppose the receivers are more tolerant than stated in the RDS spec.

One way to measure the ppm error is to play the `pulses.wav` file: it will play a pulse for precisely 1 second, then play a 1-second silence, and so on. Record the audio output from a radio with a good audio card. Say you sample at 44.1 kHz. Measure 10 intervals. Using [Audacity](https://www.audacityteam.org/) for example determine the number of samples of these 10 intervals: in the absence of clock error, it should be 441,000 samples. With my Pi, I found 441,132 samples. Therefore, my ppm error is (441132-441000)/441000 * 1e6 = 299 ppm, **assuming that my sampling device (audio card) has no clock error...**

### Non-ASCII characters

You can use the full range of characters supported by the RDS protocol. PiFMX decodes
the input strings based on the system's locale variables. As of early 2024, Raspberry Pi
OS uses by default UTF-8 and the `LANG` variable is set to `en_GB.UTF-8`. With this setup,
it should work out of the box.

If it does not work, look at the first message that PiFMX prints out. It should be
something sensible, like:

```
Locale set to en_GB.UTF-8.
```

If it is not consistent with your setup, or if the locale appears to be set to `(null)`,
then your locale variables are not set correctly and PiFMX is incapable of working
with non-ASCII characters.

### Piping audio into PiFMX

If you use the argument `-audio -`, PiFMX reads audio data on standard input. This allows you to pipe the output of a program into PiFMX. For instance, this can be used to read MP3 files using Sox:

```
sox -t mp3 http://www.linuxvoice.com/episodes/lv_s02e01.mp3 -t wav -  | sudo ./pi_fm_x -audio -
```

Or to pipe the AUX input of a sound card into PiFMX:

Before starting, we enter the command:
```
arecord -l
```
And look for our connected sound card:
```
**** List of CAPTURE Hardware Devices ****
card 1: Device [USB PnP Sound Device], device 0: USB Audio [USB Audio]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
```
We get a value of 1,0 (`-Dplughw:1,0`):
```
card 1: Device [USB PnP Sound Device], device 0: USB Audio [USB Audio]
```
Enter the obtained values ​​in pi_fm_x:
```
sudo arecord -fS16_LE -r 44100 -Dplughw:1,0 -c 2 -  | sudo ./pi_fm_x -audio -
```

### Control RDS (rds_ctl)

You can control RDS at run-time using a named pipe (FIFO). For this run PiFMX with the -ctl argument.

Before starting in the PIFMX folder, we create rds_ctl using the command:  
```
mkfifo rds_ctl
```

To launch through rds_ctl:  
```
sudo ./pi_fm_x -ctl rds_ctl
```

At this point, PiFMX waits until another program opens the named pipe in write mode (for example cat >rds_ctl in the example below) before it starts transmitting.  

You can use the named pipe to send “commands” to change RDS. For instance, in another terminal:
```
cat >rds_ctl
```
```
PI 0000
PTY 10
PS MyText
RT A text to be sent as radiotext
TA OFF/ON
TP OFF/ON
ECC E0
LIC 20
DI S/SA/SD/SC/A/AC/AD/C/CA/CD/D/ACD/ACDS
MS M/S
PIN 1,12,20
```

### PS and RT modes (rds_ctl)
I also have a special script that allows you to use different PS and RT modes:
[PiFMPSRT](https://github.com/KOTYA8/PiFMPSRT)

# Changelog
All previous versions are available in the repository: [PiFMX_VER](https://github.com/KOTYA8/PiFMX_VER)  

### **Currently**  
* **V4** - Support **LIC**, **PIN**. Management has appeared via `rds_ctl`: **LIC, PIN**  
