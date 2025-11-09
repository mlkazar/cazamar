#!/bin/csh
if ( $#argv == 0) then
   echo "usage: install.sh <destination dir>"
   exit 1
endif
mkdir $argv[1]
cp home*.html login*.html README.html sapi*.html upload*.html $argv[1]
cp uptest $argv[1]/upload
cp test_cert.pem test_key.pem $argv[1]
echo "Everything installed in " $argv[1]. " Run 'upload 7701' and then visit localhost:7701"
