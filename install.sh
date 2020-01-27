#!/bin/sh
##
## NOTE: BFT is intended to run on a 32-bit system. 
##       Do not try it on 64-bit machine.
##

# install build environment
sudo apt-get install make gcc g++ automake flex libtool bison ccache curl
# install libraries
sudo apt-get install libgmp3c2 libgmp3-dev libsqlite3-dev libber0-dev libldap2-dev
(cd /tmp && 
 curl -O -L http://google-sparsehash.googlecode.com/files/sparsehash_1.12-1_amd64.deb && 
 sudo dpkg -i sparsehash_1.12-1_amd64.deb)
#apt-get install makedepend &&
cd sfslite-1.2 &&
# add this one, if there are problems
# automake --add-missing
aclocal &&
# add this one if there are problems
# autoreconf &&
autoconf &&
libtoolize --force --copy &&
rm -rf install &&
mkdir -p install &&
export SFSHOME=`pwd` &&
./configure --prefix=$SFSHOME/install &&
make &&
make install &&
cd .. &&
rm -f sfs ; ln -s sfslite-1.2/install sfs &&
rm -f gmp ; ln -s /usr/lib gmp &&
cd src &&
make all

#rm -rf * &&
#apt-get install esvn &&
#apt-get install gcc-2.95 &&
#apt-get install automake1.4 &&
#apt-get install autoconf &&
#apt-get install bison-1.35 &&
#apt-get install g++-2.95 &&
#apt-get install libgmp3-dev &&
#apt-get install libtool &&
#apt-get install rsync &&
#apt-get install make &&
#apt-get install xutils &&
#export BFTHOME=`pwd` &&
#mkdir -p utils &&
#cd utils &&
#svn co --username quema https://proton.inrialpes.fr/svn/quema/BFT/utils . &&
#cd sfs-0.5.0
#export SFSHOME=`pwd` &&
#sh setup &&
#groupadd sfs 
#mkdir -p /export/home/sfs &&
#useradd -d /export/home/sfs -g sfs sfs 
#mkdir ./install &&
#env CC=gcc-2.95 CXX=g++-2.95 ./configure --prefix=${SFSHOME}/install &&
#make &&
#make check &&
#make install &&
#cd ${SFSHOME}/install/include ; rm -f sfs ; ln -s sfs-0.5 sfs &&
#rm -f ${SFSHOME}/install/include/sfs/rabin.h
#cp -f ${BFTHOME}/utils/rabin.h ${SFSHOME}/install/include/sfs/ &&
#cd ${BFTHOME}
#mkdir -p code &&
#cd code &&
#svn co --username quema https://proton.inrialpes.fr/svn/quema/BFT/code . &&
#rm -f sfs ; ln -s ${SFSHOME}/install sfs &&
#rm -f gmp ; ln -s /usr/lib gmp &&
#./compile_all.sh
