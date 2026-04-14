echo "file=@$PWD/../data/${2}"
echo "${1}"
curl -F "file=@$PWD/../data/${2}" ${1}/upload
