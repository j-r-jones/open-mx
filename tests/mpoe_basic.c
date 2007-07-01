#include <stdio.h>
#include <sys/time.h>

#include "mpoe_lib.h"

#define IFNAME "lo"
#define EP 3
#define ITER 10

static mpoe_return_t
send_tiny(struct mpoe_endpoint * ep, struct mpoe_mac_addr * dest_addr,
	  int i)
{
  union mpoe_request * request;
  struct mpoe_status status;
  char buffer[12];
  mpoe_return_t ret;

  sprintf(buffer, "message %d", i);

  ret = mpoe_isend(ep, buffer, strlen(buffer) + 1,
		   0x1234567887654321ULL, dest_addr, EP,
		   NULL, &request);
  if (ret != MPOE_SUCCESS) {
    fprintf(stderr, "Failed to send a tiny message (%s)\n",
	    mpoe_strerror(ret));
    return ret;
  }
  fprintf(stderr, "Successfully sent tiny \"%s\"\n", (char*) buffer);

  ret = mpoe_wait(ep, &request, &status);
  if (ret != MPOE_SUCCESS) {
    fprintf(stderr, "Failed to wait for completion (%s)\n",
	    mpoe_strerror(ret));
    return ret;
  }

  fprintf(stderr, "Successfully waited for send completion\n");

  return MPOE_SUCCESS;
}

static int
send_medium(struct mpoe_endpoint * ep, struct mpoe_mac_addr * dest_addr,
	  int i)
{
  union mpoe_request * request;
  struct mpoe_status status;
  char buffer[4096];
  mpoe_return_t ret;

  sprintf(buffer, "message %d is much longer than in a tiny buffer !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!", i);

  ret = mpoe_isend(ep, buffer, strlen(buffer) + 1,
		   0x1234567887654321ULL, dest_addr, EP,
		   NULL, &request);
  if (ret != MPOE_SUCCESS) {
    fprintf(stderr, "Failed to send a medium message (%s)\n",
	    mpoe_strerror(ret));
    return ret;
  }

  fprintf(stderr, "Successfully sent medium \"%s\"\n", (char*) buffer);

  ret = mpoe_wait(ep, &request, &status);
  if (ret != MPOE_SUCCESS) {
    fprintf(stderr, "Failed to wait for completion (%s)\n",
	    mpoe_strerror(ret));
    return ret;
  }

  fprintf(stderr, "Successfully waited for send completion\n");

  return MPOE_SUCCESS;
}

int main(void)
{
  struct mpoe_endpoint * ep;
  struct mpoe_mac_addr dest_addr;
  struct timeval tv1, tv2;
  int i;
  mpoe_return_t ret;

  ret = mpoe_open_endpoint(0, EP, &ep);
  if (ret != MPOE_SUCCESS) {
    fprintf(stderr, "Failed to open endpoint (%s)\n",
	    mpoe_strerror(ret));
    goto out;
  }

  fprintf(stderr, "Successfully open endpoint %d/%d\n", 0, EP);

  mpoe_mac_addr_set_bcast(&dest_addr);

  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {
    /* send a tiny message */
    ret = send_tiny(ep, &dest_addr, i);
    if (ret != MPOE_SUCCESS)
      goto out_with_ep;
  }
  gettimeofday(&tv2, NULL);
  printf("tiny latency %lld us\n",
	 (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec));

  gettimeofday(&tv1, NULL);
  for(i=0; i<ITER; i++) {
    /* send a medium message */
    ret = send_medium(ep, &dest_addr, i);
    if (ret != MPOE_SUCCESS)
      goto out_with_ep;
  }
  gettimeofday(&tv2, NULL);
  printf("medium latency %lld us\n",
	 (tv2.tv_sec-tv1.tv_sec)*1000000ULL+(tv2.tv_usec-tv1.tv_usec));

  return 0;

 out_with_ep:
  mpoe_close_endpoint(ep);
 out:
  return -1;
}
