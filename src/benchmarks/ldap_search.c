#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <lber.h>
#include <ldap.h>
#include "ldap_search.h"


int do_ldap_search(char *base,char *filter,char **result) 
{

	LDAP *ld;
	int  result_status;
	int  auth_method = LDAP_AUTH_SIMPLE;
	int desired_version = LDAP_VERSION3;
	//char *ldap_host = "127.0.0.1";
	char *root_dn = "cn=admin,dc=exper,dc=abstracts,dc=emulab,dc=net";
	char *root_pw = "secret";

	BerElement* ber;
	LDAPMessage* msg;
	LDAPMessage* entry;

//	  base="dc=exper,dc=abstracts,dc=emulab,dc=net";
//	  filter="(objectClass=*)";
	char* errstring;
	char* dn = NULL;
	char* attr;
	char** vals;
	char tmp_result[4096]="";
	char *res=NULL;
	int i;
	char *attr_types[]={"mobile",NULL};
	//for(int kk=0; kk<1000;kk++)
	//{


	//Initilizing LDAP connction
	//Changed from ldap_init to ldap_initialize since the old one is 
	//depricated; and we used unix sockets instead of TCP; since 
	//client is calling salpd server locally 
//	if ((ld = ldap_init("ldapi://"/*ldap_host*/, LDAP_PORT)) == NULL ) 
	if (ldap_initialize(&ld,"ldapi://%2Fvar%2Frun%2Fldapi") != LDAP_SUCCESS ) 
	{
		perror( "ldap_init failed" );
		exit( EXIT_FAILURE );
	}
	
	if (ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &desired_version) != LDAP_OPT_SUCCESS)
	{
		ldap_perror(ld, "ldap_set_option");
		exit(EXIT_FAILURE);
	}

	/*if (ldap_simple_bind_s(ld, root_dn, root_pw) != LDAP_SUCCESS )*/
	/*{*/
		/*ldap_perror( ld, "ldap_bind problems" );*/
		/*exit( EXIT_FAILURE );*/
	/*}*/


	//Synchronous search
	/*fprintf(stderr, "Filter is [%d][%s]\n", strlen(filter),filter);*/
	/*if (ldap_search_s(ld, base, LDAP_SCOPE_SUBTREE, "uid=user.999",attr_types, 0, &msg) != LDAP_SUCCESS)*/
	if (ldap_search_s(ld, base, LDAP_SCOPE_SUBTREE, filter,attr_types, 0, &msg) != LDAP_SUCCESS)
	{
		ldap_perror( ld, "ldap_search_s" );
		exit(EXIT_FAILURE);
	}

	//printf("The number of entries returned was %d\n\n", ldap_count_entries(ld, msg));

	//Parsing LDAPMEssage msg into  single string
	
	for(entry = ldap_first_entry(ld, msg); entry != NULL; entry = ldap_next_entry(ld, entry)) 
	{
		if((dn = ldap_get_dn(ld, entry)) != NULL) 
		{
			//printf("Returned dn: %s\n", dn);
			//strcat(tmp_result,"<dn:");
			//strcat(tmp_result,dn);
			//strcat(tmp_result,">\n");
			
			ldap_memfree(dn);
		}

//		fprintf(stderr,"tmp_result is : \n");
		for( attr = ldap_first_attribute(ld, entry, &ber); attr != NULL; attr = ldap_next_attribute(ld, entry, ber)) 
		{
			if ((vals = ldap_get_values(ld, entry, attr)) != NULL)  
			{
				for(i = 0; vals[i] != NULL; i++) 
				{
					//printf("%s: %s\n", attr, vals[i]);
				
					//strcat(tmp_result,"<");
					//strcat(tmp_result,attr);
					//strcat(tmp_result,":");
					strcat(tmp_result,vals[i]);
					strcat(tmp_result,",");
				}
				ldap_value_free(vals);
			}

			ldap_memfree(attr);
		}
		strcat(tmp_result,"\n");
		
//		fprintf(stderr,"tmp_result is : %s\n",tmp_result);
		if (ber != NULL) 
		{
			ber_free(ber,0);
		}

		//printf("\n");
	}

	/* clean up */
	ldap_msgfree(msg);
	result_status = ldap_unbind_s(ld);

	if (result_status != 0) 
	{
		fprintf(stderr, "ldap_search.c:ldap_unbind_s: %s\n", ldap_err2string(result_status));
		exit( EXIT_FAILURE );
	}

		
	if((res=(char*)malloc(strlen(tmp_result)*sizeof(char)+1))==NULL)
	{
		fprintf(stderr,"ldap_search.c: error in do_ldap_search; failed to malloc!");
	      	return EXIT_FAILURE;
	}
	else strcpy(res,tmp_result);	

	*result=res;
	

	return EXIT_SUCCESS;
}
