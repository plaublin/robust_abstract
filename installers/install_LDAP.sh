#!/bin/bash
#This scripts installs LDAP all stuff

# Arguments: size of file by K entries: like 10 or 100

#DIR
HOME_DIR=/proj/abstracts/BFT-Ali/installers
CONF_DIR=${HOME_DIR}/configs
cd ${HOME_DIR}

###Uninstall previous openldap servers and Berkely DB
#sudo apt-get -y remove --purge slapd ldap-utils 
#
##Install Berkeley DB from source
#pushd db-4.7.25.NC/build_unix
#sudo make install
#popd
#sudo cp ${CONF_DIR}/dbBer.conf /etc/ld.so.conf.d/dbBer.conf
#sudo ldconfig
#
##Install OpenLDAP from source
#pushd "${HOME_DIR}/openldap-2.4.21"
#sudo make install
#popd
#
## Update system commands db
#rehash
#
#sudo /usr/local/libexec/slapd 
#
###Move config files
#sudo cp ${CONF_DIR}/ldap.conf /usr/local/etc/openldap/
#sudo cp ${CONF_DIR}/slapd.conf /usr/local/etc/openldap/
##sudo mkdir /var/run/slapd/
#sudo chmod +r /usr/local/etc/openldap/*
#
##
##Update DB root password
#pass=`sudo slappasswd -s secret`
#echo $pass
#sudo sed -i "s/{SSHA}\(.*\)/$pass/" /usr/local/etc/openldap/slapd.conf
##
##
### start slapd server using ldapi also (allow unix sockets)
#sudo /usr/local/libexec/slapd -h "ldapi://%2Fusr%2Flocal%2Fvar%2Frun%2Fldapi/ ldap:///"
#sleep 1
#
#Remove old DB if any
if [ -d /dev/shm/ldap ]
then
sudo rm -rf /dev/shm/ldap
fi
#Create new DB dir
sudo mkdir /dev/shm/ldap
sudo mkdir /var/run/slapd
sudo chmod +r /dev/shm/ldap/
sudo chmod +r /var/run/slapd

#Initialize DB
sudo slapadd -l ${HOME_DIR}/init.ldif
sleep 1

#Restart slapd server
sudo kill -INT `cat /usr/local/var/run/slapd.pid`
sudo /usr/local/libexec/slapd -h "ldapi://%2Fusr%2Flocal%2Fvar%2Frun%2Fldapi/ ldap:///"
sleep 1

#Test
sudo ldapsearch -x -b "dc=exper,dc=abstracts,dc=emulab,dc=net"
sleep 1


if [ ${1} ]
then
#Import large DB
sudo ldapadd -x -w secret -D "cn=admin,dc=exper,dc=abstracts,dc=emulab,dc=net" -f ${HOME_DIR}/sample${1}K.ldif
#Test
sudo ldapsearch -x -b "dc=exper,dc=abstracts,dc=emulab,dc=net" "uid=user.9999"
sudo ldapsearch -x -b "dc=exper,dc=abstracts,dc=emulab,dc=net" "uid=user.100000"
fi
