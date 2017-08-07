#include <stdio.h>
#include <readline/readline.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#define ERR -1
#define OK 1

#define ERR_LEN 128
#define STR_LEN 64
#define BUF_SIZE 128

struct settings {
	char *host;
	int port;
};
static struct settings settings;

void set_err(char *err, char *fmt, ...) {
	if(!err) return;
	va_list va;
	va_start(va, fmt);
	vsnprintf(err, ERR_LEN, fmt, va);
	va_end(va);
}

void show_err(char *err) {
	printf("%s\n", err);
	exit(1);
}

int net_connect(char *err, char *addr, int port) {
	int s;
	struct sockaddr_in sa; 
	if((s = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		set_err(err, "creating socke fail: %s\n", strerror(errno));
		return ERR;
	}
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	inet_aton(addr, &sa.sin_addr);
	if(connect(s, (struct sockaddr*)&sa, sizeof(sa)) == -1) {
		set_err(err, "connect err: %s\n", strerror(errno));
		close(s);
		return ERR;
	}
	return s;
}

int net_write(int fd, char *buf, int count) {
	int total = 0;
	while(total != count) {
		int nwritten = write(fd, buf, count - total);
		if(nwritten == 0) return total;
		if(nwritten == -1) return -1;
		total += nwritten;
		buf += nwritten;
	}
	return total;
}

typedef struct str {
	char *content;
	int size;
	int len;
} str;


str *str_new(char *content) {
	str *s = malloc(sizeof(str));
	s->len = strlen(content);
	s->size = (s->len > STR_LEN) ? (s->len * 2) : STR_LEN;
	s->content = malloc(s->size + 1);
	if(s->len > 0) {
		strcpy(s->content, content);
	}
	return s;
}

str *str_empty() {
	return str_new("");
}

str *str_add(str *s, char *a, int len) {
	int nlen = s->len + len;
	if(nlen > s->size) {
		s->size = nlen * 2;
		s->content = realloc(s->content, s->size + 1);
	}else {
		s->size = nlen;
	}
	s->len = nlen;
	a[len] = '\0';
	s->content = strcat(s->content, a);
	return s;
}

void str_del(str *s) {
	free(s->content);
	free(s);
}

str *net_read(int fd) {
	int nread;
	char buf[BUF_SIZE];
	str *s = str_empty();
	int expected = BUF_SIZE - 1;
	while(1) {
		nread = read(fd, buf, expected);
		if(nread == 0 || nread == -1) break;
		s = str_add(s, buf, nread);
		if(nread < expected) break;
	}
	return s;
}

char *append_str(char *str, char* suffix) {
	str  = realloc(str, strlen(str) + strlen(suffix) + 1);
	str = strcat(str, suffix);
	return str;
}

char *copy_str(char *s) {
	char *c = malloc(strlen(s) + 1);
	strcpy(c, s);
	return c;
}

typedef struct node {
	struct node *next;
	char *content;
} node;

typedef struct list {
	int len;
	node *head;
	node *tail;
} list;

node *node_new(char *content) {
	node *n = malloc(sizeof(node));
	n->next = NULL;
	n->content = copy_str(content);
	return n;
}

list *list_new() {
	list *l = malloc(sizeof(list));
	l->len = 0;
	l->head = NULL;
	l->tail = NULL;
	return l;
}

int list_add(list *l, node *n) {
	if(l->len == 0) {
		l->head = n;
		l->tail = n;
	}else if(l->len == 1) {
		l->head->next = n;
		l->tail = n;
	}else {
		l->tail->next = n;
		l->tail = n;
	}
	l->len++;
	return l->len;
}

node *list_get(list *l, int index) {
	if(index > l->len - 1) return NULL;
	node *cur = l->head;
	for(int i = 0; i < l->len; i++) {
		if(cur == NULL) break;
		if(index == i) return cur;
		cur = cur->next;
	}
	return NULL;
}

void node_del(node *n) {
	free(n->content);
	free(n);
}

void list_flush(list *l) {
	if(l->len == 0) return;
	node *cur = l->head;
	node *next = cur->next;
	while(next) {
		node_del(cur);
		cur = next;
		next = cur->next;
	}
	node_del(cur);
	free(l);
}

list *split_str(char *input, char *delim) {
	list *l = list_new();
	char *arg = strtok(input, delim);
	while(arg) {
		node *n = node_new(arg);
		list_add(l, n);
		arg = strtok(NULL, delim);
	}
	return l;
}

int get_int_len(int n) {
	char str[20];
	sprintf(str, "%d", n);
	int len = strlen(str);
	return len;
}

char *get_cmd(char *input) {
	char *cmd;
	if(strstr(input, "set")) {
		list *args = split_str(input, " ");
		char *key = list_get(args, 1)->content;
		char *val = list_get(args, 2)->content;
		char *ttl = (args->len > 3) ? list_get(args, 3)->content : "0";
		int len = strlen(val);
		int size = strlen(key) + strlen(ttl) + get_int_len(len) + strlen(val) + 10;
		cmd = malloc(size + 1);
		sprintf(cmd, "set %s 0 %s %d\r\n%s", key, ttl, len, val);
		list_flush(args);
	}else {
		cmd = copy_str(input);
	}
	cmd = append_str(cmd, "\r\n");
	return cmd;
}

int read_settings(char *err, int argc, char ** argv) {
	if(argc < 3) {
		set_err(err, "Usage:memcache-cli <host> <port>");
		return ERR;
	}
	settings.host = argv[1];
	settings.port = atoi(argv[2]);
	return OK;
}

int main(int argc, char **argv) {
	puts("Memcache-cli version 0.0.6");
	puts("Enter ctrl+c to exit\n");
	
	char err[ERR_LEN];
	if(read_settings(err, argc, argv) == ERR) show_err(err);
	
	int fd = net_connect(err, settings.host, settings.port);

	char *input;
	str *result;
	char *cmd;
	while(1) {
		input = readline("> ");
		add_history(input);
		cmd = get_cmd(input);
		net_write(fd, cmd, strlen(cmd));
		result = net_read(fd);
		printf("%s", result->content);	
		str_del(result);
		free(input);
		free(cmd);
	}
	return 0;
}

