/***************************************************************************
 *                                  _   _ ____  _
 *  Project                     ___| | | |  _ \| |
 *                             / __| | | | |_) | |
 *                            | (__| |_| |  _ <| |___
 *                             \___|\___/|_| \_\_____|
 *
 * Copyright (C) Daniel Stenberg, <daniel@haxx.se>, et al.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution. The terms
 * are also available at https://curl.se/docs/copyright.html.
 *
 * You may opt to use, copy, modify, merge, publish, distribute and/or sell
 * copies of the Software, and permit persons to whom the Software is
 * furnished to do so, under the terms of the COPYING file.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 * SPDX-License-Identifier: curl
 *
 ***************************************************************************/
/* <DESC>
 * A basic application source code using the multi interface doing two
 * transfers in parallel.
 * </DESC>
 */

#include <stdio.h>
#include <string.h>

/* somewhat unix-specific */
#include <sys/time.h>
#include <unistd.h>
#include <time.h>

/* curl stuff */
#include <curl/curl.h>

/*
 * Download an HTTP file and upload an FTP file simultaneously.
 */

#define REQBODYSIZE 1048576 /* Request body size (=1MiB) */
#define HANDLECOUNT 511     /* Number of simultaneous transfers */

static char req_body[REQBODYSIZE + 1];

typedef struct {
  const char *req_body;
  size_t bytes_sent;
} read_req_body_data_t;

size_t
read_req_body(char *buffer, size_t size, size_t nitems, void *userdata)
{
  read_req_body_data_t *data = (read_req_body_data_t *)userdata;
  size_t to_send             = size * nitems;
  memcpy(buffer, data->req_body + data->bytes_sent, to_send);

  /* sleep 1ms */
  struct timespec sleep_time;
  sleep_time.tv_sec  = 0;
  sleep_time.tv_nsec = 1 * 1000 * 1000; // 1ms
  nanosleep(&sleep_time, NULL);

  data->bytes_sent += to_send;
  fprintf(stderr, "to_send=%lu, bytes_sent=%lu\n", to_send, data->bytes_sent);
  return to_send;
}

int
main(void)
{
  read_req_body_data_t data[HANDLECOUNT];
  CURL *handles[HANDLECOUNT];
  CURLM *multi_handle;

  memset(req_body, 'a', sizeof(req_body) - 1);
  req_body[sizeof(req_body) - 1] = '\0';

  int still_running = 1; /* keep number of running handles */
  int i;

  CURLMsg *msg;  /* for picking up messages with the transfer status */
  int msgs_left; /* how many messages are left */

  CURLcode res;

  /* Allocate one CURL handle per transfer */
  for (i = 0; i < HANDLECOUNT; i++) {
    data[i].req_body   = req_body;
    data[i].bytes_sent = 0;
    handles[i]         = curl_easy_init();

    res = curl_easy_setopt(handles[i], CURLOPT_URL, "http://localhost/limit-mem");
    if (res != CURLE_OK) {
      fprintf(stderr, "cannot set url: %s", curl_easy_strerror(res));
    }
    res = curl_easy_setopt(handles[i], CURLOPT_INFILESIZE_LARGE, sizeof(req_body) - 1);
    if (res != CURLE_OK) {
      fprintf(stderr, "cannot set request body length: %s\n", curl_easy_strerror(res));
    }
    struct curl_slist *headers = NULL;
    headers                    = curl_slist_append(headers, "Content-Type: text/plain");
    res                        = curl_easy_setopt(handles[i], CURLOPT_HTTPHEADER, headers);
    if (res != CURLE_OK) {
      fprintf(stderr, "cannot set request header: %s\n", curl_easy_strerror(res));
    }
    res = curl_easy_setopt(handles[i], CURLOPT_READFUNCTION, read_req_body);
    if (res != CURLE_OK) {
      fprintf(stderr, "cannot set request read function: %s\n", curl_easy_strerror(res));
    }
    res = curl_easy_setopt(handles[i], CURLOPT_UPLOAD, 1L);
    if (res != CURLE_OK) {
      fprintf(stderr, "cannot enable upload: %s\n", curl_easy_strerror(res));
    }
    res = curl_easy_setopt(handles[i], CURLOPT_READDATA, &data[i]);
    if (res != CURLE_OK) {
      fprintf(stderr, "cannot set request read data: %s\n", curl_easy_strerror(res));
    }
  }

  /* init a multi stack */
  multi_handle = curl_multi_init();

  /* add the individual transfers */
  CURLMcode mc;
  for (i = 0; i < HANDLECOUNT; i++) {
    mc = curl_multi_add_handle(multi_handle, handles[i]);
    if (mc != CURLM_OK) {
      fprintf(stderr, "cannot add handle: %s\n", curl_multi_strerror(mc));
    }
  }

  while (still_running) {
    mc = curl_multi_perform(multi_handle, &still_running);

    if (still_running)
      /* wait for activity, timeout or "nothing" */
      mc = curl_multi_poll(multi_handle, NULL, 0, 1000, NULL);

    if (mc != CURLM_OK) {
      fprintf(stderr, "cannot poll handle: %s\n", curl_multi_strerror(mc));
      break;
    }
  }
  /* See how the transfers went */
  while ((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
    if (msg->msg == CURLMSG_DONE) {
      int idx;

      /* Find out which handle this message is about */
      for (idx = 0; idx < HANDLECOUNT; idx++) {
        int found = (msg->easy_handle == handles[idx]);
        if (found)
          break;
      }

      printf("HTTP transfer %d completed with status %d\n", idx, msg->data.result);
    }
  }

  /* remove the transfers and cleanup the handles */
  for (i = 0; i < HANDLECOUNT; i++) {
    mc = curl_multi_remove_handle(multi_handle, handles[i]);
    if (mc != CURLM_OK) {
      fprintf(stderr, "cannot remove handle: %s\n", curl_multi_strerror(mc));
    }
    curl_easy_cleanup(handles[i]);
  }

  mc = curl_multi_cleanup(multi_handle);
  if (mc != CURLM_OK) {
    fprintf(stderr, "cannot remove handle: %s\n", curl_multi_strerror(mc));
  }

  return 0;
}
