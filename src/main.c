#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <sys/time.h>
#include <unistd.h>
#include <time.h>

#include <ketopt.h>
#include <khash.h>
#include <kvec.h>
#include <curl/curl.h>

#define REQBODYSIZE 1048576 /* Request body size (=1MiB) */

static char common_req_body[REQBODYSIZE + 1];

typedef struct {
  const char *req_body;
  size_t bytes_sent;
  size_t delay_ns;
} read_req_body_data_t;

typedef kvec_t(read_req_body_data_t) kvec_read_req_body_data_t;
typedef kvec_t(CURL *) kvec_curl_ptr_t;

#define NS_PER_SEC (1000 * 1000 * 1000)

static size_t
read_req_body(char *buffer, size_t size, size_t nitems, void *userdata)
{
  read_req_body_data_t *data = (read_req_body_data_t *)userdata;
  size_t to_send             = size * nitems;
  memcpy(buffer, data->req_body + data->bytes_sent, to_send);

  if (data->delay_ns > 0) {
    struct timespec sleep_time;
    sleep_time.tv_sec  = data->delay_ns / NS_PER_SEC;
    sleep_time.tv_nsec = data->delay_ns % NS_PER_SEC;
    nanosleep(&sleep_time, NULL);
  }

  data->bytes_sent += to_send;
  fprintf(stderr, "to_send=%lu, bytes_sent=%lu\n", to_send, data->bytes_sent);
  return to_send;
}

static size_t
discard_response_body(char *ptr, size_t size, size_t nmemb, void *userdata)
{
  return size * nmemb;
}

static bool
init_handle(kvec_curl_ptr_t *handles, kvec_read_req_body_data_t *req_body_userdata, int i, const char *url,
            struct curl_slist *headers, struct curl_slist *resolve_list, size_t delay_ns)
{
  kv_A(*req_body_userdata, i).req_body   = common_req_body;
  kv_A(*req_body_userdata, i).bytes_sent = 0;
  kv_A(*req_body_userdata, i).delay_ns   = delay_ns;
  kv_A(*handles, i)                      = curl_easy_init();

  CURLcode res;
  res = curl_easy_setopt(kv_A(*handles, i), CURLOPT_URL, url);
  if (res != CURLE_OK) {
    fprintf(stderr, "cannot set url: %s", curl_easy_strerror(res));
    goto error;
  }
  if (resolve_list != NULL) {
    res = curl_easy_setopt(kv_A(*handles, i), CURLOPT_RESOLVE, resolve_list);
    if (res != CURLE_OK) {
      fprintf(stderr, "cannot set resolve options: %s\n", curl_easy_strerror(res));
      goto error;
    }
  }
  res = curl_easy_setopt(kv_A(*handles, i), CURLOPT_INFILESIZE_LARGE, sizeof(common_req_body) - 1);
  if (res != CURLE_OK) {
    fprintf(stderr, "cannot set request body length: %s\n", curl_easy_strerror(res));
    goto error;
  }
  res = curl_easy_setopt(kv_A(*handles, i), CURLOPT_HTTPHEADER, headers);
  if (res != CURLE_OK) {
    fprintf(stderr, "cannot set request header: %s\n", curl_easy_strerror(res));
    goto error;
  }
  res = curl_easy_setopt(kv_A(*handles, i), CURLOPT_READFUNCTION, read_req_body);
  if (res != CURLE_OK) {
    fprintf(stderr, "cannot set request read function: %s\n", curl_easy_strerror(res));
    goto error;
  }
  res = curl_easy_setopt(kv_A(*handles, i), CURLOPT_WRITEFUNCTION, discard_response_body);
  if (res != CURLE_OK) {
    fprintf(stderr, "cannot set request write function: %s\n", curl_easy_strerror(res));
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
  OPT_RESOLVE,
  OPT_DELAY,
  OPT_HELP,
};

#define MIN_CONCURRENCY     1
#define MAX_CONCURRENCY     511
#define DEFAULT_CONCURRENCY MAX_CONCURRENCY

KHASH_MAP_INIT_INT(khash_map_int_int_t, int)

static bool
has_suffix(const char *s, const char *suffix)
{
  int s_len      = strlen(s);
  int suffix_len = strlen(suffix);
  return s_len >= suffix_len && strcmp(s + (s_len - suffix_len), suffix) == 0;
}

int
main(int argc, char *argv[])
{
  const char *url     = NULL;
  int concurrency     = DEFAULT_CONCURRENCY;
  const char *resolve = NULL;
  size_t delay_ns     = 0;

  static ko_longopt_t longopts[] = {
    {"url",         ko_required_argument, OPT_URL        },
    {"concurrency", ko_required_argument, OPT_CONCURRENCY},
    {"resolve",     ko_required_argument, OPT_RESOLVE    },
    {"delay",       ko_required_argument, OPT_DELAY      },
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
    case OPT_RESOLVE:
      if (resolve != NULL) {
        fprintf(stderr, "multiple --resolve options not supported.\n");
      }
      resolve = opt.arg;
      break;
    case OPT_DELAY: {
      size_t unit    = 0;
      int suffix_len = 0;
      if (has_suffix(opt.arg, "ms")) {
        unit       = 1000 * 1000;
        suffix_len = 2;
      } else if (has_suffix(opt.arg, "s")) {
        unit       = 1000 * 1000 * 1000;
        suffix_len = 1;
      } else {
        fprintf(stderr, "unit of delay must be \"s\" or \"ms\".\n");
        return 2;
      }
      char *end;
      long delay_num = strtol(opt.arg, &end, 10);
      if (*(end + suffix_len) != '\0' || delay_num < 0) {
        fprintf(stderr, "delay must be non-negative integer.\n");
        return 2;
      }
      delay_ns = delay_num * unit;
      break;
    }
    case 'h':
    case OPT_HELP:
      fprintf(stderr, "Usage: %s [OPTIONS]\n\n", argv[0]);
      fprintf(stderr, "-u, --url <url>                          Target URL (required).\n");
      fprintf(stderr, "-c, --concurrency <concurrency>          Number of clients (between 1 and 511, default 511).\n");
      fprintf(stderr, "--resolve <[+]host:port:addr[,addr]...>  Provide a custom address for a specific host and port pair.\n");
      fprintf(stderr, "--delay <delay>                          delay in writing request body chunks (ex. 1s, 5ms).\n");
      fprintf(stderr, "-h, --help                               Show this help.\n");
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

  struct curl_slist *headers = NULL;
  headers                    = curl_slist_append(headers, "Content-Type: text/plain");
  if (headers == NULL) {
    goto exit3;
  }

  struct curl_slist *resolve_list = NULL;
  if (resolve != NULL) {
    resolve_list = curl_slist_append(resolve_list, resolve);
    if (resolve_list == NULL) {
      goto exit4;
    }
  }

  int n_inited_handles = 0;
  for (; n_inited_handles < concurrency; n_inited_handles++) {
    if (!init_handle(&handles, &req_body_userdata, n_inited_handles, url, headers, resolve_list, delay_ns)) {
      goto exit5;
    }
  }

  int n_added_handles = 0;
  CURLM *multi_handle = curl_multi_init();
  if (multi_handle == NULL) {
    fprintf(stderr, "cannot init curl_multi handle.\n");
    goto exit6;
  }
  CURLMcode mc;
  for (; n_added_handles < concurrency; n_added_handles++) {
    mc = curl_multi_add_handle(multi_handle, kv_A(handles, n_added_handles));
    if (mc != CURLM_OK) {
      fprintf(stderr, "cannot add handle: %s\n", curl_multi_strerror(mc));
      goto exit6;
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
      goto exit6;
    }
  }

  /* See how the transfers went */
  int absent;
  khint_t k;
  khash_t(khash_map_int_int_t) *h = kh_init(khash_map_int_int_t);
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

      long http_code = 0;
      curl_easy_getinfo(kv_A(handles, i), CURLINFO_RESPONSE_CODE, &http_code);
      k = kh_put(khash_map_int_int_t, h, (khint32_t)http_code, &absent);
      if (absent) {
        kh_value(h, k) = 1;
      } else {
        kh_value(h, k) = kh_value(h, k) + 1;
      }
      printf("HTTP transfer %d completed with status %d\n", i, msg->data.result);
    }
  }
  for (k = kh_begin(h); k != kh_end(h); ++k) {
    if (kh_exist(h, k)) {
      printf("status:%d, count:%d\n", kh_key(h, k), kh_value(h, k));
    }
  }
  kh_destroy(khash_map_int_int_t, h);

  exit_code = 0;

exit6:
  for (int i = 0; i < n_added_handles; ++i) {
    mc = curl_multi_remove_handle(multi_handle, kv_A(handles, i));
    if (mc != CURLM_OK) {
      fprintf(stderr, "cannot remove handle: %s\n", curl_multi_strerror(mc));
    }
  }
  mc = curl_multi_cleanup(multi_handle);
  if (mc != CURLM_OK) {
    fprintf(stderr, "cannot remove handle: %s\n", curl_multi_strerror(mc));
  }
exit5:
  for (int i = 0; i < n_inited_handles; ++i) {
    curl_easy_cleanup(kv_A(handles, i));
  }
  curl_slist_free_all(resolve_list);
exit4:
  curl_slist_free_all(headers);
exit3:
  kv_destroy(handles);
exit2:
  kv_destroy(req_body_userdata);
exit1:
  return exit_code;
}
