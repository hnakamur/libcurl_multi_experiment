#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <sys/time.h>
#include <unistd.h>
#include <time.h>

#include <ketopt.h>
#include <kvec.h>
#include <curl/curl.h>

#define REQBODYSIZE 1048576 /* Request body size (=1MiB) */

static char common_req_body[REQBODYSIZE + 1];

typedef struct {
  const char *req_body;
  size_t bytes_sent;
} read_req_body_data_t;

typedef kvec_t(read_req_body_data_t) kvec_read_req_body_data_t;
typedef kvec_t(CURL *) kvec_curl_ptr_t;

static size_t
read_req_body(char *buffer, size_t size, size_t nitems, void *userdata)
{
  read_req_body_data_t *data = (read_req_body_data_t *)userdata;
  size_t to_send             = size * nitems;
  memcpy(buffer, data->req_body + data->bytes_sent, to_send);

  /* sleep 1ms */
  struct timespec sleep_time;
  sleep_time.tv_sec  = 0;
  sleep_time.tv_nsec = 1 * 1000 * 1000;
  nanosleep(&sleep_time, NULL);

  data->bytes_sent += to_send;
  fprintf(stderr, "to_send=%lu, bytes_sent=%lu\n", to_send, data->bytes_sent);
  return to_send;
}

static bool
init_handle(kvec_curl_ptr_t *handles, kvec_read_req_body_data_t *req_body_userdata, int i)
{
  kv_A(*req_body_userdata, i).req_body   = common_req_body;
  kv_A(*req_body_userdata, i).bytes_sent = 0;
  kv_A(*handles, i)                      = curl_easy_init();

  CURLcode res;
  res = curl_easy_setopt(kv_A(*handles, i), CURLOPT_URL, "http://localhost/limit-mem");
  if (res != CURLE_OK) {
    fprintf(stderr, "cannot set url: %s", curl_easy_strerror(res));
    goto error;
  }
  res = curl_easy_setopt(kv_A(*handles, i), CURLOPT_INFILESIZE_LARGE, sizeof(common_req_body) - 1);
  if (res != CURLE_OK) {
    fprintf(stderr, "cannot set request body length: %s\n", curl_easy_strerror(res));
    goto error;
  }
  struct curl_slist *headers = NULL;
  headers                    = curl_slist_append(headers, "Content-Type: text/plain");
  res                        = curl_easy_setopt(kv_A(*handles, i), CURLOPT_HTTPHEADER, headers);
  if (res != CURLE_OK) {
    fprintf(stderr, "cannot set request header: %s\n", curl_easy_strerror(res));
    goto error;
  }
  res = curl_easy_setopt(kv_A(*handles, i), CURLOPT_READFUNCTION, read_req_body);
  if (res != CURLE_OK) {
    fprintf(stderr, "cannot set request read function: %s\n", curl_easy_strerror(res));
    goto error;
  }
  res = curl_easy_setopt(kv_A(*handles, i), CURLOPT_UPLOAD, 1L);
  if (res != CURLE_OK) {
    fprintf(stderr, "cannot enable upload: %s\n", curl_easy_strerror(res));
    goto error;
  }
  res = curl_easy_setopt(kv_A(*handles, i), CURLOPT_READDATA, &kv_A(*req_body_userdata, i));
  if (res != CURLE_OK) {
    fprintf(stderr, "cannot set request read data: %s\n", curl_easy_strerror(res));
    goto error;
  }
  return true;
error:
  curl_easy_cleanup(kv_A(*handles, i));
  return false;
}

enum {
  OPT_URL = 301,
  OPT_CONCURRENCY,
  OPT_HELP,
};

#define MIN_CONCURRENCY     1
#define MAX_CONCURRENCY     511
#define DEFAULT_CONCURRENCY MAX_CONCURRENCY

int
main(int argc, char *argv[])
{
  const char *url = NULL;
  int concurrency = DEFAULT_CONCURRENCY;

  static ko_longopt_t longopts[] = {
    {"url",         ko_required_argument, OPT_URL        },
    {"concurrency", ko_required_argument, OPT_CONCURRENCY},
    {"help",        ko_no_argument,       OPT_HELP       },
    {NULL,          0,                    0              }
  };
  ketopt_t opt = KETOPT_INIT;
  int c;
  while ((c = ketopt(&opt, argc, argv, 1, "u:c:h", longopts)) >= 0) {
    switch (c) {
    case 'u':
    case OPT_URL:
      url = opt.arg;
      break;
    case 'c':
    case OPT_CONCURRENCY: {
      char *end;
      long conc = strtol(opt.arg, &end, 10);
      if (*end != '\0' || conc < MIN_CONCURRENCY || conc > MAX_CONCURRENCY) {
        fprintf(stderr, "concurrency must be integer between %d and %d.\n", MIN_CONCURRENCY, MAX_CONCURRENCY);
        return 2;
      }
      concurrency = (int)conc;
      break;
    }
    case 'h':
    case OPT_HELP:
      fprintf(stderr, "Usage: %s [OPTIONS]\n\n", argv[0]);
      fprintf(stderr, "-u, --url:          target URL (required).\n");
      fprintf(stderr, "-c, --concurrency:  number of clients (between 1 and 511, default 511).\n");
      fprintf(stderr, "-h, --help:         show this help.\n");
      return 2;
    }
  }
  if (url == NULL) {
    fprintf(stderr, "option --url must be specified.\n");
    return 2;
  }
  printf("url=%s, concurrency=%d\n", url, concurrency);

  int exit_code = 1;
  kvec_read_req_body_data_t req_body_userdata;
  kv_init(req_body_userdata);
  if (kv_resize(read_req_body_data_t, req_body_userdata, concurrency) == NULL) {
    fprintf(stderr, "cannot allocate req_body_userdata vector.\n");
    goto exit1;
  }
  kvec_curl_ptr_t handles;
  kv_init(handles);
  if (kv_resize(CURL *, handles, concurrency) == NULL) {
    fprintf(stderr, "cannot allocate handles vector.\n");
    goto exit2;
  }

  memset(common_req_body, 'a', sizeof(common_req_body) - 1);
  common_req_body[sizeof(common_req_body) - 1] = '\0';

  CURLM *multi_handle = curl_multi_init();
  if (multi_handle == NULL) {
    fprintf(stderr, "cannot init curl_multi handle.\n");
    goto exit3;
  }

  CURLMcode mc;
  int n_inited_handles;
  for (n_inited_handles = 0; n_inited_handles < concurrency; n_inited_handles++) {
    if (!init_handle(&handles, &req_body_userdata, n_inited_handles)) {
      goto exit4;
    }
  }

  int n_added_handles;
  for (n_added_handles = 0; n_added_handles < concurrency; n_added_handles++) {
    mc = curl_multi_add_handle(multi_handle, kv_A(handles, n_added_handles));
    if (mc != CURLM_OK) {
      fprintf(stderr, "cannot add handle: %s\n", curl_multi_strerror(mc));
      goto exit5;
    }
  }

  int still_running = 1;
  while (still_running) {
    mc = curl_multi_perform(multi_handle, &still_running);

    if (still_running)
      /* wait for activity, timeout or "nothing" */
      mc = curl_multi_poll(multi_handle, NULL, 0, 1000, NULL);

    if (mc != CURLM_OK) {
      fprintf(stderr, "cannot poll handle: %s\n", curl_multi_strerror(mc));
      goto exit5;
    }
  }

  /* See how the transfers went */
  CURLMsg *msg;  /* for picking up messages with the transfer status */
  int msgs_left; /* how many messages are left */
  while ((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
    if (msg->msg == CURLMSG_DONE) {
      /* Find out which handle this message is about */
      int i;
      for (i = 0; i < concurrency; i++) {
        int found = (msg->easy_handle == kv_A(handles, i));
        if (found)
          break;
      }

      printf("HTTP transfer %d completed with status %d\n", i, msg->data.result);
    }
  }

  exit_code = 0;

exit5:
  for (int i = 0; i < n_added_handles; ++i) {
    mc = curl_multi_remove_handle(multi_handle, kv_A(handles, i));
    if (mc != CURLM_OK) {
      fprintf(stderr, "cannot remove handle: %s\n", curl_multi_strerror(mc));
    }
  }
exit4:
  for (int i = 0; i < n_inited_handles; ++i) {
    curl_easy_cleanup(kv_A(handles, i));
  }
  mc = curl_multi_cleanup(multi_handle);
  if (mc != CURLM_OK) {
    fprintf(stderr, "cannot remove handle: %s\n", curl_multi_strerror(mc));
  }
exit3:
  kv_destroy(handles);
exit2:
  kv_destroy(req_body_userdata);
exit1:
  return exit_code;
}
