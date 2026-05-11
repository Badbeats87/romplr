#!/bin/bash
# Deploy 128 curated SF2 files to the Pi via scp
# Files are numbered 000-127 so alphabetical sort = program change order
#
# Usage: ./deploy_sf2.sh [PI_IP]

PI_IP="${1:-192.168.0.94}"
PI_USER="root"
PI_DIR="/root/sf2"
FANTOM="$HOME/Downloads/Roland Fantom X SoundFont"
STAGING="/tmp/rompler_sf2"

rm -rf "$STAGING"
mkdir -p "$STAGING"

copy_sf2() {
    local num="$1" name="$2" cat="$3" file="$4"
    local src="$FANTOM/$cat/$file"
    local dst=$(printf "%s/%03d_%s.sf2" "$STAGING" "$num" "$name")
    if [ ! -f "$src" ]; then
        echo "MISSING: $src"
        return 1
    fi
    cp "$src" "$dst"
}

echo "Staging 128 SF2 files..."

# Acoustic Pianos 0-9
copy_sf2 0   "Concert_Grand"   "00 Ac.Piano"  "F001 ConcertGrand.sf2"
copy_sf2 1   "Pure_Grand"      "00 Ac.Piano"  "F010 X Pure Grand.sf2"
copy_sf2 2   "So_True"         "00 Ac.Piano"  "A001 So True....sf2"
copy_sf2 3   "Hall_Concert"    "00 Ac.Piano"  "F002 Hall Concert.sf2"
copy_sf2 4   "Warm_Piano"      "00 Ac.Piano"  "A003 Warm Piano.sf2"
copy_sf2 5   "Dark_Grand"      "00 Ac.Piano"  "F016 Dark Grand.sf2"
copy_sf2 6   "JD800_Piano"     "00 Ac.Piano"  "A008 JD-800 Piano.sf2"
copy_sf2 7   "Studio_Grand"    "00 Ac.Piano"  "F011 Studio Grand.sf2"
copy_sf2 8   "Dry_Studio_88"   "00 Ac.Piano"  "F013 DryStudio88.sf2"
copy_sf2 9   "Arie_Piano"      "00 Ac.Piano"  "F020 Arie Piano.sf2"

# Electric Pianos 10-29
copy_sf2 10  "Stage_EP"        "01 El.Piano"  "F007 Stage EP.sf2"
copy_sf2 11  "Mk2_Stage_Phsr"  "01 El.Piano"  "A025 Mk2 Stg phsr.sf2"
copy_sf2 12  "EP_mkI"          "01 El.Piano"  "F006 EP mkI.sf2"
copy_sf2 13  "FM_EP"           "01 El.Piano"  "A035 FM EP.sf2"
copy_sf2 14  "FM_777"          "01 El.Piano"  "A036 FM-777.sf2"
copy_sf2 15  "FM_EPad"         "01 El.Piano"  "A037 FM EPad.sf2"
copy_sf2 16  "1983_EP"         "01 El.Piano"  "F033 1983 EP.sf2"
copy_sf2 17  "FS_Wurly"        "01 El.Piano"  "A030 FS Wurly.sf2"
copy_sf2 18  "Mr_AXXE"         "01 El.Piano"  "F032 Mr.AXXE.sf2"
copy_sf2 19  "Age_n_Tines"     "01 El.Piano"  "A019 Age'n'Tines.sf2"
copy_sf2 20  "Celestial_EP"    "01 El.Piano"  "A022 Celestial EP.sf2"
copy_sf2 21  "EP_Stack"        "01 El.Piano"  "F034 EP Stack.sf2"
copy_sf2 22  "Chocolate_EP"    "01 El.Piano"  "F036 Chocolate EP.sf2"
copy_sf2 23  "Abstract_EP"     "01 El.Piano"  "F037 Abstract EP.sf2"
copy_sf2 24  "LEO_EP"          "01 El.Piano"  "A017 LEO EP.sf2"
copy_sf2 25  "Stage_Cabinet"   "01 El.Piano"  "F029 StageCabinet.sf2"
copy_sf2 26  "Super_Wurly"     "01 El.Piano"  "A032 Super Wurly.sf2"
copy_sf2 27  "Pulse_EPno"      "01 El.Piano"  "A033 Pulse EPno.sf2"
copy_sf2 28  "Hipchord"        "01 El.Piano"  "F039 Hipchord.sf2"
copy_sf2 29  "Balladeer"       "01 El.Piano"  "A028 Balladeer.sf2"

# Synth Leads 30-49
copy_sf2 30  "Jupiter_Lead"    "22 Soft Lead"  "G092 Jupiter Lead.sf2"
copy_sf2 31  "Jupi_Square"     "22 Soft Lead"  "G094 Jupi Square.sf2"
copy_sf2 32  "JP_Saw_Lead"     "22 Soft Lead"  "C068 JP Saw Lead.sf2"
copy_sf2 33  "Mini_Growl"      "22 Soft Lead"  "G091 Mini Growl.sf2"
copy_sf2 34  "Actual_Analog"   "21 Hard Lead"  "G087 ActualAnalog.sf2"
copy_sf2 35  "Legato_Saw"      "21 Hard Lead"  "C082 Legato Saw.sf2"
copy_sf2 36  "Pure_Square"     "21 Hard Lead"  "C081 Pure Square.sf2"
copy_sf2 37  "Classic_Lead"    "21 Hard Lead"  "G071 Classic Lead.sf2"
copy_sf2 38  "Lone_Prophat"    "21 Hard Lead"  "C083 Lone Prophat.sf2"
copy_sf2 39  "MG_Sqr_Lead"     "22 Soft Lead"  "C069 MG Sqr Lead.sf2"
copy_sf2 40  "FS_Saw_Lead_1"   "21 Hard Lead"  "C085 FS Saw Ld 1.sf2"
copy_sf2 41  "FS_Saw_Lead_2"   "21 Hard Lead"  "C086 FS Saw Ld 2.sf2"
copy_sf2 42  "DC_Triangle"     "21 Hard Lead"  "C078 DC Triangle.sf2"
copy_sf2 43  "Mogulator"       "21 Hard Lead"  "C091 Mogulator Ld.sf2"
copy_sf2 44  "Tristar"         "22 Soft Lead"  "C070 Tristar.sf2"
copy_sf2 45  "Epic_Lead"       "21 Hard Lead"  "G080 Epic Lead.sf2"
copy_sf2 46  "X_Saw_Lead"      "21 Hard Lead"  "G086 X-Saw Lead.sf2"
copy_sf2 47  "Mid_Saw_Lead"    "22 Soft Lead"  "C060 Mid Saw Ld.sf2"
copy_sf2 48  "FS_Sqr_Lead"     "22 Soft Lead"  "C056 FS Sqr Lead.sf2"
copy_sf2 49  "FS_Soft_Lead"    "22 Soft Lead"  "C059 FS SoftLead.sf2"

# Synth Pads 50-69
copy_sf2 50  "Jupiter_2004"    "27 Bright Pad"  "H082 Jupiter 2004.sf2"
copy_sf2 51  "Jupiter_X"       "28 Soft Pad"    "H095 Jupiter-X.sf2"
copy_sf2 52  "JP8_Phase"       "28 Soft Pad"    "H101 JP-8 Phase.sf2"
copy_sf2 53  "JP_Strings_1"    "28 Soft Pad"    "D106 JP Strings 1.sf2"
copy_sf2 54  "JP_Strings_2"    "28 Soft Pad"    "D107 JP Strings 2.sf2"
copy_sf2 55  "OB_Slow_Str"     "28 Soft Pad"    "D110 OB Slow Str.sf2"
copy_sf2 56  "Super_SynStr"    "28 Soft Pad"    "D111 Super SynStr.sf2"
copy_sf2 57  "OB_Rezo_Pad"     "27 Bright Pad"  "E009 OB Rezo Pad.sf2"
copy_sf2 58  "Warm_Reso_Pad"   "28 Soft Pad"    "D103 WarmReso Pad.sf2"
copy_sf2 59  "Cloud_9"         "28 Soft Pad"    "D126 Cloud #9.sf2"
copy_sf2 60  "Silk_Pad"        "28 Soft Pad"    "D102 Silk Pad.sf2"
copy_sf2 61  "FS_Soft_Pad"     "28 Soft Pad"    "D104 FS Soft Pad.sf2"
copy_sf2 62  "Digi_Swell"      "27 Bright Pad"  "E005 Digi-Swell.sf2"
copy_sf2 63  "Electric_Pad"    "27 Bright Pad"  "E018 Electric Pad.sf2"
copy_sf2 64  "Voyager"         "27 Bright Pad"  "E020 Voyager.sf2"
copy_sf2 65  "Cosmic_Rays"     "27 Bright Pad"  "E021 Cosmic Rays.sf2"
copy_sf2 66  "JD_Pop_Pad"      "28 Soft Pad"    "H099 JD Pop Pad.sf2"
copy_sf2 67  "Flange_Dream"    "28 Soft Pad"    "H104 Flange Dream.sf2"
copy_sf2 68  "OB_Aaahs"        "28 Soft Pad"    "D124 OB Aaahs.sf2"
copy_sf2 69  "Organic_Pad"     "28 Soft Pad"    "D128 Organic Pad.sf2"

# Techno/Trance 70-79
copy_sf2 70  "JP8000_Saws"     "26 Other Synth"   "H048 JP-8000 Saws.sf2"
copy_sf2 71  "X_Super_Saws"    "26 Other Synth"   "H049 X Super Saws.sf2"
copy_sf2 72  "SuperSaw_Slow"   "26 Other Synth"   "D052 SuperSawSlow.sf2"
copy_sf2 73  "Trance_Saws"     "26 Other Synth"   "D053 TranceSaws.sf2"
copy_sf2 74  "Saw_Stack"       "26 Other Synth"   "D055 Saw Stack.sf2"
copy_sf2 75  "JP_Oct_Attack"   "26 Other Synth"   "D061 JP OctAttack.sf2"
copy_sf2 76  "FM_Attack"       "26 Other Synth"   "D065 FM's Attack.sf2"
copy_sf2 77  "Trancy_Synth"    "26 Other Synth"   "D054 Trancy Synth.sf2"
copy_sf2 78  "Anadroid"        "23 Techno Synth"  "G108 Anadroid.sf2"
copy_sf2 79  "Waving_TB303"    "23 Techno Synth"  "D025 Waving TB303.sf2"

# Pulsating 80-89
copy_sf2 80  "Mr_Fourier"      "24 Pulsating"  "H005 Mr. Fourier.sf2"
copy_sf2 81  "Step_Trance"     "24 Pulsating"  "E034 Step Trance.sf2"
copy_sf2 82  "Auto_Trance"     "24 Pulsating"  "E037 Auto Trance.sf2"
copy_sf2 83  "FS_Throbulax"    "24 Pulsating"  "E056 FS Throbulax.sf2"
copy_sf2 84  "FS_Strobe"       "24 Pulsating"  "E058 FS Strobe.sf2"
copy_sf2 85  "Strobe_X"        "24 Pulsating"  "H002 Strobe X.sf2"
copy_sf2 86  "Auto_5th_Saws"   "24 Pulsating"  "H012 Auto 5thSaws.sf2"
copy_sf2 87  "Square_Sphere"   "24 Pulsating"  "H023 SquareSphere.sf2"
copy_sf2 88  "Shroomy"         "23 Techno Synth"  "G109 Shroomy.sf2"
copy_sf2 89  "Synth_Force"     "24 Pulsating"  "E032 Synth Force.sf2"

# Synth Bass 90-109
copy_sf2 90  "SQ_Pan_Bass"     "12 Synth Bass"  "B075 SQ Pan.sf2"
copy_sf2 91  "MC404_Bass"      "12 Synth Bass"  "B039 MC-404 Bass.sf2"
copy_sf2 92  "SH101_Bass_1"    "12 Synth Bass"  "B040 SH-101 Bs 1.sf2"
copy_sf2 93  "SH101_Bass_2"    "12 Synth Bass"  "B057 SH-101 Bs 2.sf2"
copy_sf2 94  "SH1_Bass"        "12 Synth Bass"  "B056 SH-1 Bass.sf2"
copy_sf2 95  "Electro_Rubber"  "12 Synth Bass"  "B042 Electro Rubb.sf2"
copy_sf2 96  "Smooth_Bass"     "12 Synth Bass"  "B038 Smooth Bass.sf2"
copy_sf2 97  "Solid_Goa"       "12 Synth Bass"  "B054 Solid Goa.sf2"
copy_sf2 98  "Poly_Bass"       "12 Synth Bass"  "B059 Poly Bass.sf2"
copy_sf2 99  "Punch_MG_1"      "12 Synth Bass"  "B060 Punch MG 1.sf2"
copy_sf2 100 "Punch_MG_2"      "12 Synth Bass"  "B066 Punch MG 2.sf2"
copy_sf2 101 "Super_G_DX"      "12 Synth Bass"  "B065 Super-G DX.sf2"
copy_sf2 102 "Q_Bass"          "12 Synth Bass"  "B062 Q Bass.sf2"
copy_sf2 103 "Kickin_Bass"     "12 Synth Bass"  "B067 Kickin' Bass.sf2"
copy_sf2 104 "Saw_MG_Bass"     "12 Synth Bass"  "B048 Saw&MG Bass.sf2"
copy_sf2 105 "FS_Syn_Bass_1"   "12 Synth Bass"  "B041 FS Syn Bass1.sf2"
copy_sf2 106 "FS_Syn_Bass_2"   "12 Synth Bass"  "B058 FS Syn Bass2.sf2"
copy_sf2 107 "FS_Rubber_Bass"  "12 Synth Bass"  "B063 FS Rubber Bs.sf2"
copy_sf2 108 "Reso_Syn_Bs_1"   "12 Synth Bass"  "B055 ResoSyn Bs 1.sf2"
copy_sf2 109 "Reso_Syn_Bs_2"   "12 Synth Bass"  "B064 ResoSyn Bs 2.sf2"

# Organs 110-115
copy_sf2 110 "Full_Draw_Organ" "05 Organ"  "A075 FullDraw Org.sf2"
copy_sf2 111 "Perc_Organ"      "05 Organ"  "F055 X Perc Organ.sf2"
copy_sf2 112 "Purple_Organ"    "05 Organ"  "F057 Purple Organ.sf2"
copy_sf2 113 "60s_Organ"       "05 Organ"  "A088 60's Org 1.sf2"
copy_sf2 114 "Chapel_Organ"    "05 Organ"  "A091 Chapel Organ.sf2"
copy_sf2 115 "Grand_Pipe"      "05 Organ"  "A092 Grand Pipe.sf2"

# Strings 116-119
copy_sf2 116 "Full_Strings"    "13 Strings"  "G019 Full Strings.sf2"
copy_sf2 117 "Oct_Strings"     "13 Strings"  "G021 Oct Strings.sf2"
copy_sf2 118 "Chamber_Str"     "13 Strings"  "B097 Chamber Str.sf2"
copy_sf2 119 "Warm_Strings"    "13 Strings"  "B104 Warm Strings.sf2"

# Bells 120-123
copy_sf2 120 "Glockenspiel"    "03 Bells"  "A051 FS Glocken.sf2"
copy_sf2 121 "Music_Box"       "03 Bells"  "A053 FS Musicbox.sf2"
copy_sf2 122 "Tubular_Bell"    "03 Bells"  "A064 Tubular Bell.sf2"
copy_sf2 123 "Himalaya_Ice"    "03 Bells"  "A056 Himalaya Ice.sf2"

# Brass 124-125
copy_sf2 124 "FS_Brass"        "18 Ac.Brass"  "C020 FS Brass.sf2"
copy_sf2 125 "Tp_Tb_Sect"      "18 Ac.Brass"  "C019 TpTb Sect..sf2"

# Extra Synths 126-127
copy_sf2 126 "Modular_Lead"    "22 Soft Lead"  "G096 Modular Lead.sf2"
copy_sf2 127 "Motion_Pad"      "28 Soft Pad"   "H089 Motion Pad.sf2"

COUNT=$(ls "$STAGING"/*.sf2 2>/dev/null | wc -l | tr -d ' ')
SIZE=$(du -sh "$STAGING" | cut -f1)
echo ""
echo "Staged $COUNT/128 files ($SIZE) in $STAGING"
echo ""

if [ "$COUNT" -ne 128 ]; then
    echo "ERROR: Not all files staged. Fix missing files above."
    exit 1
fi

echo "Deploying to $PI_USER@$PI_IP:$PI_DIR ..."
ssh "$PI_USER@$PI_IP" "mkdir -p $PI_DIR"
scp -r "$STAGING"/*.sf2 "$PI_USER@$PI_IP:$PI_DIR/"

echo ""
echo "Done! To run on Pi:"
echo "  ssh $PI_USER@$PI_IP"
echo "  ./rompler $PI_DIR/ -m /dev/snd/midiC2D0"
