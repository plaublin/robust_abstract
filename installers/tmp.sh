#!/bin/bash

for i in {0..11}
do
#ssh client$i 'sudo cp /proj/abstracts/bft-ldap/installers/sysctl.conf /etc/sysctl.conf'
ssh client$i 'sudo cp /proj/abstracts/bft-ldap/installers/openldap-2.4.21/libraries/libldap/.libs/libldap-2.4.so.2 /usr/local/lib'
ssh client$i 'sudo cp /proj/abstracts/bft-ldap/installers/openldap-2.4.21/libraries/liblber/.libs/liblber-2.4.so.2 /usr/local/lib'
ssh client$i 'sudo ldconfig'
#ssh client$i 'sudo /etc/init.d/network restart&'
#ssh client$i 'sudo ldconfig'
done

