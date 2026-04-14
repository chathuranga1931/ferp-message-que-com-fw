echo "file=@$PWD/data/${2}"
curl -F "file=@$PWD/data/${2}" ${1}/upload
