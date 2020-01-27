#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <lber.h>
#include <ldap.h>

int
do_ldap_search(char *base,char *filter,char **result);

#if OWNER==1 
#define EXTERN
#else
#define EXTERN extern
#endif

EXTERN char *ldap_host; 
