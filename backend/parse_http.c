/**
 * Copyright (C) 2022 Carnegie Mellon University
 *
 * This file is part of the HTTP course project developed for
 * the Computer Networks course (15-441/641) taught at Carnegie
 * Mellon University.
 *
 * No part of the HTTP project may be copied and/or distributed
 * without the express permission of the 15-441/641 course staff.
 */
#include <string.h>
#include "parse_http.h"
#include <sys/stat.h>
#include <unistd.h>

#include <dirent.h>

void trim_whitespace(char *input, size_t length) {
    if (input == NULL) {
        fprintf(stderr, "Error: Input string is NULL.\n");
        return;
    }

    // Find the index of the first non-whitespace character from the beginning
    size_t start = 0;
    while (start < length && isspace(input[start])) {
        start++;
    }

    // Find the index of the first non-whitespace character from the end
    size_t end = length - 1;
    while (end > start && isspace(input[end])) {
        end--;
    }

    // Calculate the length of the trimmed string
    size_t trimmed_length = (end >= start) ? (end - start + 1) : 0;

    // Allocate memory for the trimmed string
    char *result = (char *)malloc((trimmed_length + 1) * sizeof(char));
    if (result == NULL) {
        fprintf(stderr, "Error: Memory allocation failed.\n");
        return;
    }

    // Copy the trimmed portion of the input string to the result
    strncpy(result, input + start, trimmed_length);

    // Add the null terminator at the end
    result[trimmed_length] = '\0';

    // Copy the trimmed string back.
    // Note: there should not be another null temrinator in the string.
    // Be careful about how strcpy() works: https://linux.die.net/man/3/strcpy
    strcpy(input, result);
    // Make sure to free the temporary string.
    free(result);

    return;
}

void to_lower(char *str, size_t str_len) {
    for (int i = 0; i < str_len; i++) {
        str[i] = tolower(str[i]);
    }
}


/**
 * Wrapper for serializing an error HTTP response.
 * @param pointer message
 * @param response_type 
 */
char *serialize_http_response_wrapper(size_t *len, const char *response_type) {
    char *resp_text;
    serialize_http_response(&resp_text, len, response_type, NULL, NULL, NULL, 0, NULL);
    return resp_text;
}

// get file size
// @param char* file_path (path to resource)
size_t get_file_size(char *file_path) {
    struct stat st;
    // to-do add error handling robustness
    stat(file_path, &st);
    return st.st_size;
}


/**
* Given a char buffer returns the parsed request headers
*/
char *process_http_request(Request *request, size_t *len, char *base_folder) {
    static const char *index_str = "/index.html";

    char *http_resource_path = request->http_uri;

    // unexpected behavior in this method...
    if (http_resource_path[0] != '/') {
        printf("NO RESOURCE indicated \n");
        return serialize_http_response_wrapper(len, BAD_REQUEST);
    }

    // concating base dir and file path
    int path_len = strlen(base_folder) + strlen(http_resource_path); 

    char * resource_path = (char *)malloc(path_len + strlen(index_str) + 1);
    strcpy(resource_path, base_folder);
    strcat(resource_path, http_resource_path); 

    if (access(resource_path, F_OK) != 0) {
        printf("RESOURCE NOT FOUND: %s\n", resource_path);
        return serialize_http_response_wrapper(len, NOT_FOUND);
    }

    /* check if resource_path points to a directory */

    printf("resource requested: %s\n", resource_path);
    DIR *dir = opendir(resource_path);
    if(dir != NULL) {
      // int s = (resource_path[strlen(resource_path) - 1] == '/');
      // strcat(resource_path, index_str + s);
      strcat(resource_path, index_str);
      printf("Directory requested, new request is %s\n", resource_path);
      closedir(dir);
    }

    FILE *resource_file = fopen(resource_path, "rb");
    if (resource_file == NULL)  {
        return serialize_http_response_wrapper(len, INTERNAL_SERVER_ERROR);
    }

    size_t resource_file_size = get_file_size(resource_path);

    char *resource_file_content = malloc(resource_file_size);
    if (resource_file_content == NULL) {
        fclose(resource_file);
        free(resource_path);
        return serialize_http_response_wrapper(len, INTERNAL_SERVER_ERROR);
    }

    fread(resource_file_content, 1, resource_file_size, resource_file);
    
    char content_length_str[32];
    sprintf(content_length_str, "%zu", resource_file_size);
    
    char *response;
    serialize_http_response(&response, len, OK, NULL, content_length_str, NULL, resource_file_size, resource_file_content);

    fclose(resource_file);
    free(resource_file_content);
    free(resource_path);

    return response;
}


/**
* Given a char buffer returns the parsed request headers
*/
test_error_code_t parse_http_request(char *buffer, size_t size, Request *request) {
  //Differant states in the state machine
	enum {
		STATE_START = 0, STATE_CR, STATE_CRLF, STATE_CRLFCR, STATE_CRLFCRLF
	};

	int i = 0, state;
	size_t offset = 0;
	char ch;
	// Safe to assume that every valid request has header smaller than
	// 8192 bytes. This buf does not hold body content.
	char buf[8192];
	memset(buf, 0, 8192);

	state = STATE_START;
	while (state != STATE_CRLFCRLF) {
		char expected = 0;

		if (i == size)
			break;

		ch = buffer[i++];
		buf[offset++] = ch;

		switch (state) {
		case STATE_START:
		case STATE_CRLF:
			expected = '\r';
			break;
		case STATE_CR:
		case STATE_CRLFCR:
			expected = '\n';
			break;
		default:
			state = STATE_START;
			continue;
		}

		if (ch == expected)
			state++;
		else
			state = STATE_START;

	}

    //Valid End State
	if (state == STATE_CRLFCRLF) {
		request->header_count = 0;
		request->status_header_size = 0;
		request->allocated_headers = 15;
		request->headers = (Request_header *) malloc(sizeof(Request_header) * request->allocated_headers);
		set_parsing_options(buf, i, request);

		yyrestart(NULL);
		if (yyparse() == SUCCESS) {
		    request->valid = true;
		    for (int i = 0; i < request->header_count; ++i) {
			    Request_header *header = &request->headers[i];
			    trim_whitespace(header->header_name, strlen(header->header_name));
			    to_lower(header->header_name, strlen(header->header_name));
			    trim_whitespace(header->header_value, strlen(header->header_value));
			    to_lower(header->header_value, strlen(header->header_value));
		    }
		    return TEST_ERROR_NONE;
		}
		return TEST_ERROR_PARSE_FAILED;
	}
	return TEST_ERROR_PARSE_PARTIAL;
}

/**
 * Given a request returns the serialized char* buffer
 */
test_error_code_t serialize_http_request(char *buffer, size_t *size, Request *request) {
    memset(buffer, 0, HTTP_SIZE);
    char* p = buffer;
    if (strcmp(request->http_method, GET) != 0) {
        return TEST_ERROR_PARSE_FAILED;
    }
    memcpy(p, request->http_method, strlen(request->http_method));
    p[strlen(request->http_method)] = ' ';
    p += strlen(request->http_method) + 1;
    *size += strlen(request->http_method) + 1;
    
    memcpy(p, request->http_uri, strlen(request->http_uri));
    p[strlen(request->http_uri)] = ' ';
    p += strlen(request->http_uri) + 1;
    *size += strlen(request->http_uri) + 1;

    memcpy(p, HTTP_VER, strlen(HTTP_VER));
    p += strlen(HTTP_VER);
    *size += strlen(HTTP_VER);

    memcpy(p, CRLF, strlen(CRLF));
    p += strlen(CRLF);
    *size += strlen(CRLF);

    memcpy(p, HOST, strlen(HOST));
    p += strlen(HOST);
    *size += strlen(HOST);

    memcpy(p, request->host, strlen(request->host));
    p += strlen(request->host);
    *size += strlen(request->host);

    memcpy(p, CRLF, strlen(CRLF));
    p += strlen(CRLF);
    *size += strlen(CRLF);

    memcpy(p, CONNECTION, strlen(CONNECTION));
    p += strlen(CONNECTION);
    *size += strlen(CONNECTION);

    // memcpy(p, CLOSE, strlen(CLOSE));
    // p += strlen(CLOSE);
    // *size += strlen(CLOSE);
    memcpy(p, CONNECTION_VAL, strlen(CONNECTION_VAL));
    p += strlen(CONNECTION_VAL);
    *size += strlen(CONNECTION_VAL);

    memcpy(p, CRLF, strlen(CRLF));
    p += strlen(CRLF);
    *size += strlen(CRLF);

    memcpy(p, CRLF, strlen(CRLF));
    p += strlen(CRLF);
    *size += strlen(CRLF);

    return TEST_ERROR_NONE;
}

/**
 * Given a char buffer returns the parsed request headers
 */
test_error_code_t serialize_http_response(char **msg, size_t *len, const char *prepopulated_headers, char *content_type,
                   char *content_length, char *last_modified, size_t body_len, char *body) {
    char date[4096];
    time_t now;
    time(&now);
    struct tm *now_tm = localtime(&now);
    strftime(date, 4096, "%a, %d %b %Y %H:%M:%S %Z", now_tm);

    size_t date_len = strlen(date);
    size_t content_type_len = content_type == NULL ? 0 : strlen(content_type);
    size_t content_length_len = content_length == NULL ? 0 : strlen(content_length);
    size_t last_modified_len = last_modified == NULL ? 0 : strlen(last_modified);

    size_t prepopulated_len = strlen(prepopulated_headers);
   size_t msg_len = prepopulated_len + strlen(HTTP_VER) + 1
                     + strlen(CONNECTION) + strlen(CONNECTION_VAL) + strlen(CRLF)
                     + strlen(SERVER) + strlen(SERVER_VAL) + strlen(CRLF)
                     + strlen(DATE) + date_len + strlen(CRLF);
    if (content_type != NULL) {
        msg_len += strlen(CONTENT_TYPE) + content_type_len + strlen(CRLF);
    }
    if (content_length != NULL) {
        msg_len += strlen(CONTENT_LENGTH) + content_length_len + strlen(CRLF);
    } else {
        msg_len += strlen(CONTENT_LENGTH) + strlen(ZERO) + strlen(CRLF);
    }
    if (last_modified != NULL) {
        msg_len += strlen(CONTENT_TYPE) + last_modified_len + strlen(CRLF);
    }
    msg_len += strlen(CRLF);
    msg_len += body_len;
    *len = msg_len;

    *msg = calloc(msg_len, sizeof(char));

    size_t cur_len = 0;
    // Prepopulated
    memcpy(*msg + cur_len, HTTP_VER, strlen(HTTP_VER));
    cur_len += strlen(HTTP_VER);

    memcpy(*msg + cur_len, " ", 1);
    cur_len += 1;

    memcpy(*msg + cur_len, prepopulated_headers, prepopulated_len);
    cur_len += prepopulated_len;

    // Standard headers
    cur_len += populate_header(*msg + cur_len, CONNECTION, strlen(CONNECTION), CONNECTION_VAL, strlen(CONNECTION_VAL));
    cur_len += populate_header(*msg + cur_len, SERVER, strlen(SERVER), SERVER_VAL, strlen(SERVER_VAL));
    cur_len += populate_header(*msg + cur_len, DATE, strlen(DATE), date, date_len);
    if (content_type != NULL) {
        cur_len += populate_header(*msg + cur_len, CONTENT_TYPE, strlen(CONTENT_TYPE), content_type, content_type_len);
    }
    if (content_length != NULL) {
        cur_len += populate_header(*msg + cur_len, CONTENT_LENGTH, strlen(CONTENT_LENGTH), content_length,
                                   content_length_len);
    } else {
        cur_len += populate_header(*msg + cur_len, CONTENT_LENGTH, strlen(CONTENT_LENGTH), ZERO,
                                   strlen(ZERO));
    }
    if (last_modified != NULL) {
        cur_len += populate_header(*msg + cur_len, LAST_MODIFIED, strlen(CONTENT_TYPE), last_modified,
                                   last_modified_len);
    }
    memcpy(*msg + cur_len, CRLF, strlen(CRLF));
    cur_len += strlen(CRLF);

    if (body != NULL) {
        memcpy(*msg + cur_len, body, body_len);
        cur_len += body_len;
    }
    return TEST_ERROR_NONE;
}


int populate_header(char *msg, char *field, const size_t field_len, char *val, const size_t val_len) {
    memcpy(msg, field, field_len);
    memcpy(msg + field_len, val, val_len);
    memcpy(msg + field_len + val_len, CRLF, strlen(CRLF));
    return field_len + val_len + strlen(CRLF);
}
