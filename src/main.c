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

/* curl stuff */
#include <curl/curl.h>

/*
 * Download an HTTP file and upload an FTP file simultaneously.
 */

#define REQBODYSIZE 1048576 /* Request body size (=1MiB) */
#define HANDLECOUNT 511     /* Number of simultaneous transfers */

int
main(void)
{
  static char req_body[REQBODYSIZE + 1];
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
    handles[i] = curl_easy_init();
    res        = curl_easy_setopt(handles[i], CURLOPT_URL, "http://localhost/limit-mem");
    if (res != CURLE_OK) {
      fprintf(stderr, "cannot set url: %s", curl_easy_strerror(res));
    }
    res = curl_easy_setopt(handles[i], CURLOPT_POSTFIELDS, req_body);
    if (res != CURLE_OK) {
      fprintf(stderr, "cannot set request body: %s", curl_easy_strerror(res));
    }
  }

  /* init a multi stack */
  multi_handle = curl_multi_init();

  /* add the individual transfers */
  CURLMcode mc;
  for (i = 0; i < HANDLECOUNT; i++) {
    mc = curl_multi_add_handle(multi_handle, handles[i]);
    if (mc != CURLM_OK) {
      fprintf(stderr, "cannot add handle: %s", curl_multi_strerror(mc));
    }
  }

  while (still_running) {
    mc = curl_multi_perform(multi_handle, &still_running);

    if (still_running)
      /* wait for activity, timeout or "nothing" */
      mc = curl_multi_poll(multi_handle, NULL, 0, 1000, NULL);

    if (mc != CURLM_OK) {
      fprintf(stderr, "cannot poll handle: %s", curl_multi_strerror(mc));
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
      fprintf(stderr, "cannot remove handle: %s", curl_multi_strerror(mc));
    }
    curl_easy_cleanup(handles[i]);
  }

  mc = curl_multi_cleanup(multi_handle);
  if (mc != CURLM_OK) {
    fprintf(stderr, "cannot remove handle: %s", curl_multi_strerror(mc));
  }

  return 0;
}
