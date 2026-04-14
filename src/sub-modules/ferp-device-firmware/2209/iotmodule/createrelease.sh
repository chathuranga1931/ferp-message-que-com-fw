#!/bin/bash
# is_clean=$(git status | grep "nothing to commit")
# if [[ -z $is_clean ]]; then
# 	echo "Error: Source tree has uncommited changes or untracked files, add/commit them before package the release... "
# 	exit 1
# fi

echo "Building..."
pio run

echo "Creating Keypad Release Package"

FW_VERSION_STRING=$(cat ./src/Application/version.h | grep "#define.*FW_VERSION")
#output should like below
#define FW_VERSION "1.0.5"
FW_VERSION_STR2=$(echo $FW_VERSION_STRING | sed -e "s/#define FW_VERSION //g")
FW_VERSION=$(echo $FW_VERSION_STR2 | sed -e 's/"//g')

# CFG_VERSION_STRING=$(cat ./Keypad_12_Btn/Keypad_Src/App/com_handler.c | grep "#define.*CONFIG_VERSION")
# CFG_VERSION_STR2=$(echo $CFG_VERSION_STRING | sed -e "s/#define CONFIG_VERSION //g")
# CFG_VERSION=$(echo $CFG_VERSION_STR2 | sed -e 's/"//g')

# echo $FW_VERSION
# echo $FW_VERSION_STRING
# echo $FW_VERSION_STR2
# echo $CFG_VERSION

FILE_VERSION="FW_"${FW_VERSION}

if [[ $1 == "--ignore-tag" ]]; then
	echo "Skip tagging... "
else
	is_tagged=$(git tag | grep "V$FW_VERSION")
	if [[ ! -z $is_tagged ]]; then
		echo "Version is already Tagged, Update it before proceeding"
		exit 1
	fi

	git tag "V$FW_VERSION"
	git push origin "V$FW_VERSION"
fi

FOLDER_NAME="FERP_IoT_COM_Release_"$FILE_VERSION
echo $FOLDER_NAME

#echo $FILE_VERSION

mkdir -p $FOLDER_NAME  $FOLDER_NAME/Factory $FOLDER_NAME/Release
cp ./.pio/build/esp32dev/*.bin $FOLDER_NAME/Release/
tar -czf ./${FOLDER_NAME}.tar.gz ./${FOLDER_NAME}
rm -rf $FOLDER_NAME
TODAY=$(date '+%Y%m%d')
mkdir -p ../../Releases/$TODAY
cp -r ./$FOLDER_NAME.tar.gz ../../Releases/$TODAY
rm ./$FOLDER_NAME.tar.gz
