#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "ldap_search.h"
int main(int argc, char **argv)
{

	int rc;
	char *result=NULL;
	char base[128]="";
	char filter[128]="";
	strcpy(base,argv[1]);
	strcpy(filter,argv[2]);
	/*time_t start,end,start2,end2;*/
	/*long clo=clock();*/
	/*struct timeval tv;*/
	/*gettimeofday(&tv, NULL);*/
	/*start=(1000*(unsigned int)tv.tv_sec)+(((unsigned int)tv.tv_usec)/1000);*/
	/*start2=tv.tv_sec;*/
	/*Timer timer;*/
	/*timer.start();	*/
	/*fprintf(stderr,"\n The time now is : %ld: %ld\n",start2,start);*/
	/*for(int kk=0;kk<1000;kk++)*/
	{
	rc=do_ldap_search(base,filter,&result);

	if(rc!= EXIT_SUCCESS)
	{
		fprintf(stderr,"\ntester.c: error in main(); do_ldap_search failed !!\n");
		return EXIT_FAILURE;
	}

	fprintf(stderr,"Result is *result:\n%s",result);

	if( result!=NULL)
	free(result);
	}
	result=NULL;
	/*gettimeofday(&tv, NULL);*/
	/*end=(1000*(unsigned int)tv.tv_sec)+(((unsigned int)tv.tv_usec)/1000);*/
	/*end2=tv.tv_usec;*/
	/*timer.stop();	*/
	/*fprintf(stderr,"\n The time needed was : %ld:%ld ; %f \n",end2-start2,end-start,timer.elapsed());*/

	return EXIT_SUCCESS;
}
