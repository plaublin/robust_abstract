#!/bin/bash
#This scripts installs and configures the ldap server 



# Reinstall ldap from scratch
sudo apt-get -y remove --purge slapd ldap-utils db4.2-util 
# Use predefined LDAP passwords
debconf-set-selections /proj/abstracts/bft-ldap/conf/ldap.seed 
sudo apt-get -y  install slapd ldap-utils db4.2-util 
# Movve ldap config files
sudo cp -Rp /proj/abstracts/bft-ldap/conf/ldap/slapd.conf /etc/ldap/slapd.conf
sudo cp -Rp /proj/abstracts/bft-ldap/conf/ldap/ldap.conf /etc/ldap/ldap.conf
sudo cp -Rp /proj/abstracts/bft-ldap/conf/DB_CONFIG /var/lib/ldap/DB_CONFIG



# Change permissions
sudo chmod -R 777 /etc/ldap/
sudo chown -R openldap:openldap /etc/ldap


# Update ldap rootpw password
pass=`slappasswd -s secret`

echo $pass
sudo sed -i "s/{SSHA}\(.*\)/$pass/" /etc/ldap/slapd.conf

# Inistilize DB
sudo /etc/init.d/slapd stop
sleep 1
sudo rm -rf /var/lib/ldap/*
sudo slapadd -l /proj/abstracts/bft-ldap/conf/init.ldif
#sudo slapadd -l /proj/abstracts/bft-ldap/conf/sample10K.ldif
#sudo slapadd -l /proj/abstracts/bft-ldap/conf/sample100K.ldif
sudo chown -R openldap:openldap /var/lib/ldap

#Move DB to a new directory
sudo rm -rf /dev/shm/ldap
sudo mkdir /dev/shm/ldap
sudo mv /var/lib/ldap/* /dev/shm/ldap
sudo chmod -R 777 /dev/shm/ldap
sudo chown -R openldap:openldap /dev/shm/ldap
sudo cp -p /proj/abstracts/bft-ldap/conf/ldap/slapd.conf /etc/ldap/slapd.conf

sleep 1
# Start Server
sudo /etc/init.d/slapd start
sleep 1
# Test
ldapsearch -x -b '' -s base '(objectclass=*)' namingContexts

echo Importing new DB now

#sudo ldapadd -x -w secret -D "cn=admin,dc=exper,dc=abstracts,dc=emulab,dc=net" -f /proj/abstracts/bft-ldap/conf/sample10K.ldif
#ldapsearch -x -b "dc=exper,dc=abstracts,dc=emulab,dc=net" "uid=user.9999"
sudo ldapadd -x -w secret -D "cn=admin,dc=exper,dc=abstracts,dc=emulab,dc=net" -f /proj/abstracts/bft-ldap/conf/init.ldif
ldapsearch -x -b '' -s base '(objectclass=*)' namingContexts
sleep 1
sudo ldapadd -x -w secret -D "cn=admin,dc=exper,dc=abstracts,dc=emulab,dc=net" -f /proj/abstracts/bft-ldap/conf/sample100K.ldif
ldapsearch -x -b "dc=exper,dc=abstracts,dc=emulab,dc=net" "uid=user.100000"

