#!/bin/bash

for i in {0..3}
do
ssh replica$i "cd /proj/abstracts/BFT-Ali/installers;bash;./install_LDAP.sh 100 > ~/s${i}.log "&
done


#ssh  ashoker@replica0 "sudo sh /proj/abstracts/bft-ldap/conf/init_ldap.sh  > /proj/abstracts/bft-ldap/conf/s0.log & "
#ssh  ashoker@replica1 "sudo sh /proj/abstracts/bft-ldap/conf/init_ldap.sh  > /proj/abstracts/bft-ldap/conf/s1.log & "
#ssh  ashoker@replica2 "sudo sh /proj/abstracts/bft-ldap/conf/init_ldap.sh  > /proj/abstracts/bft-ldap/conf/s2.log & "
#ssh  ashoker@replica3 "sudo sh /proj/abstracts/bft-ldap/conf/init_ldap.sh  > /proj/abstracts/bft-ldap/conf/s3.log & "
#ssh  ashoker@replica4 "sudo sh /proj/abstracts/bft-ldap/conf/init_ldap.sh  > /proj/abstracts/bft-ldap/conf/s4.log & "
#ssh  ashoker@replica5 "sudo sh /proj/abstracts/bft-ldap/conf/init_ldap.sh  > /proj/abstracts/bft-ldap/conf/s5.log & "
#ssh  ashoker@replica6 "sudo sh /proj/abstracts/bft-ldap/conf/init_ldap.sh  > /proj/abstracts/bft-ldap/conf/s6.log & "
#ssh  ashoker@replica7 "sudo sh /proj/abstracts/bft-ldap/conf/init_ldap.sh  > /proj/abstracts/bft-ldap/conf/s7.log & "
#
#ssh  ashoker@replica0 "sudo /etc/init.d/slapd stop; sudo cp -p /proj/abstracts/bft-ldap/conf/ldap/slapd.conf /etc/ldap/slapd.conf;sudo /etc/init.d/slapd restart" 
#ssh  ashoker@replica1 "sudo /etc/init.d/slapd stop; sudo cp -p /proj/abstracts/bft-ldap/conf/ldap/slapd.conf /etc/ldap/slapd.conf;sudo /etc/init.d/slapd restart" 
#ssh  ashoker@replica2 "sudo /etc/init.d/slapd stop; sudo cp -p /proj/abstracts/bft-ldap/conf/ldap/slapd.conf /etc/ldap/slapd.conf;sudo /etc/init.d/slapd restart" 
#ssh  ashoker@replica3 "sudo /etc/init.d/slapd stop; sudo cp -p /proj/abstracts/bft-ldap/conf/ldap/slapd.conf /etc/ldap/slapd.conf;sudo /etc/init.d/slapd restart" 
#ssh  ashoker@replica4 "sudo /etc/init.d/slapd stop; sudo cp -p /proj/abstracts/bft-ldap/conf/ldap/slapd.conf /etc/ldap/slapd.conf;sudo /etc/init.d/slapd restart" 
#ssh  ashoker@replica5 "sudo /etc/init.d/slapd stop; sudo cp -p /proj/abstracts/bft-ldap/conf/ldap/slapd.conf /etc/ldap/slapd.conf;sudo /etc/init.d/slapd restart" 
#ssh  ashoker@replica6 "sudo /etc/init.d/slapd stop; sudo cp -p /proj/abstracts/bft-ldap/conf/ldap/slapd.conf /etc/ldap/slapd.conf;sudo /etc/init.d/slapd restart" 
#ssh  ashoker@replica7 "sudo /etc/init.d/slapd stop; sudo cp -p /proj/abstracts/bft-ldap/conf/ldap/slapd.conf /etc/ldap/slapd.conf;sudo /etc/init.d/slapd restart" 


























