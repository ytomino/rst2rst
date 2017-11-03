#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iconv.h>

/* input */

enum { PAGE = 4096 };

static bool read_src(
	char * restrict src,
	size_t * restrict src_i,
	size_t * restrict src_len)
{
	if(*src_i >= PAGE){
		memcpy(src, src + *src_i, *src_len - *src_i);
		*src_len -= *src_i;
		*src_i = 0;
	}
	if(*src_len < PAGE){
		*src_len += fread(src + *src_len, 1, PAGE * 2 - *src_len, stdin);
	}
	return *src_i < *src_len;
}

/* parse */

static bool is_decimal(char c)
{
	return '0' <= c && c <= '9';
}

static bool is_tagname(char c)
{
	return ('a' <= c && c <= 'z') || c == '-' || is_decimal(c);
}

static void take_blank(
	char const * restrict src,
	size_t * restrict src_i,
	size_t const * restrict src_len)
{
	if(*src_i < *src_len && (src[*src_i] == ' ' || src[*src_i] == '\n')){
		++ *src_i;
	}
}

static bool take_tag(
	char * restrict src,
	size_t * restrict src_i,
	size_t * restrict src_len,
	char * restrict buf,
	size_t * restrict buf_len)
{
	read_src(src, src_i, src_len); /* for +1 */
	if(*src_i + 1 < *src_len
		&& src[*src_i] == '\\'
		&& is_tagname(src[*src_i + 1]))
	{
		buf[0] = '\\';
		buf[1] = src[*src_i + 1];
		*buf_len = 2;
		*src_i += 2;
		while(read_src(src, src_i, src_len)
			&& is_tagname(src[*src_i])
			&& *buf_len < PAGE)
		{
			buf[*buf_len] = src[*src_i];
			++ *buf_len;
			++ *src_i;
		}
		take_blank(src, src_i, src_len);
		return true;
	}else{
		return false;
	}
}

static bool take_hexadecimal_char(
	char * restrict src,
	size_t * restrict src_i,
	size_t * restrict src_len,
	char * restrict buf,
	size_t * restrict buf_len)
{
	read_src(src, src_i, src_len); /* for +1 */
	if(*src_i + 1 < *src_len && src[*src_i] == '\\' && src[*src_i + 1] == '\''){
		buf[0] = '\\';
		buf[1] = '\'';
		*buf_len = 2;
		*src_i += 2;
		while(read_src(src, src_i, src_len)
			&& is_tagname(src[*src_i])
			&& *buf_len < PAGE)
		{
			buf[*buf_len] = src[*src_i];
			++ *buf_len;
			++ *src_i;
		}
		take_blank(src, src_i, src_len);
		return true;
	}else{
		return false;
	}
}

static bool take_escaped_char(
	char * restrict src,
	size_t * restrict src_i,
	size_t * restrict src_len,
	char * restrict buf)
{
	read_src(src, src_i, src_len); /* for +1 */
	if(*src_i + 1 < *src_len && src[*src_i] == '\\'){
		++ *src_i;
		*buf = src[*src_i];
		++ *src_i;
		return true;
	}else{
		return false;
	}
}

static bool take_char(
	char const * restrict src,
	size_t * restrict src_i,
	size_t const * restrict src_len,
	char c)
{
	if(*src_i < *src_len && src[*src_i] == c){
		++ *src_i;
		return true;
	}else{
		return false;
	}
}

/* output */

static char const tocode[] = "UTF-8";

enum state { FIRST, LPAREN, RPAREN, TAG, PARAM, TEXT };

static void write_tag(
	enum state * restrict state,
	char const * restrict tag,
	size_t len)
{
	switch(*state){
	case RPAREN:
		fputc('\n', stdout);
		break;
	case TAG:
		fputc(' ', stdout);
		break;
	default:
		break;
	}
	fwrite(tag, 1, len, stdout);
	*state = TAG;
}

static void do_write_text(
	enum state * restrict state,
	bool * restrict text)
{
	switch(*state){
	case LPAREN: case RPAREN:
		fputc('\n', stdout);
		break;
	case TAG:
		if(*text){
			fputc(' ', stdout);
		}else{
			fputc('\n', stdout);
		}
		break;
	default:
		break;
	}
	*state = TEXT;
	*text = true;
}

static void write_encoded_char(
	enum state * restrict state,
	char c)
{
	char tag[PAGE];
	size_t tag_len;
	
	tag_len = sprintf(tag, "\\\'%02x", (unsigned char)c);
	write_tag(state, tag, tag_len);
}

static void write_decoded_text(
	enum state * restrict state,
	bool * restrict text,
	iconv_t cd,
	char * enc,
	size_t enc_len)
{
	char out_buf[PAGE];
	bool done = enc_len == 0;
	
	while(!done){
		char * out = out_buf;
		size_t out_left = PAGE;
		bool is_error;
		
		if(enc_len == 0){
			is_error = iconv(cd, NULL, NULL, &out, &out_left) == (size_t)-1;
			done = true;
		}else{
			is_error = iconv(cd, &enc, &enc_len, &out, &out_left) == (size_t)-1;
		}
		for(char * p = out_buf; p < out; ++p){
			if((unsigned char)*p < '\x20'){
				write_encoded_char(state, *p);
			}else{
				do_write_text(state, text);
				fwrite(p, 1, 1, stdout);
			}
		}
		if(is_error){
			write_encoded_char(state, *enc);
			++ enc;
			-- enc_len;
		}
	}
}

static void write_text(
	enum state * restrict state,
	bool * restrict text,
	char const * restrict s,
	size_t len)
{
	do_write_text(state, text);
	fwrite(s, 1, len, stdout);
}

static void write_lf(
	enum state * restrict state,
	bool * restrict text)
{
	if(*state != FIRST){
		fputc('\n', stdout);
		*state = FIRST;
		*text = false;
	}
}

int main(int argc, __attribute__((unused)) char ** argv)
{
	if(argc > 1){
		char const *arg = argv[1];
		
		if(arg[0] == '-'){
			if(arg[1] != '\0'){
				fprintf(stderr, "rtf2rtf: convert rtf to human friendly rtf\n");
				return 1;
			}
			/* if argv[1] is "-", read from stdin */
		}else{
			if(freopen(arg, "r", stdin) == NULL){
				fprintf(stderr, "rtf2rtf: %s: No such file or directory\n", arg);
				return 1;
			}
		}
	}
	
	char src[PAGE * 2];
	size_t src_len = 0;
	size_t src_i = 0;
	bool body = false;
	enum state state = FIRST;
	bool text = false;
	iconv_t iconv_cd = NULL;
	char buf[PAGE];
	size_t buf_len;
	
	while(read_src(src, &src_i, &src_len)){
		/* hexadecimal text */
		if(take_hexadecimal_char(src, &src_i, &src_len, buf, &buf_len)){
			char enc[PAGE];
			size_t enc_len = 0;
			
			if(iconv_cd == NULL){
				iconv_cd = iconv_open(tocode, "ASCII");
			}
			do{
				long c;
				char * endptr;
				
				buf[buf_len] = '\0';
				c = strtol(buf + 2, &endptr, 16);
				if(endptr != buf + 4){
					/* invalid char */
					if(enc_len > 0){
						write_decoded_text(&state, &text, iconv_cd, enc, enc_len);
						enc_len = 0;
					}
					fprintf(stderr, "invalid char: %.*s\n", (int)buf_len, buf);
					write_tag(&state, buf, buf_len);
					break;
				}else{
					enc[enc_len] = (char)c;
					++ enc_len;
				}
			}while(read_src(src, &src_i, &src_len)
				&& take_hexadecimal_char(src, &src_i, &src_len, buf, &buf_len));
			if(enc_len > 0){
				write_decoded_text(&state, &text, iconv_cd, enc, enc_len);
			}
			continue;
		}
		/* tag */
		if(take_tag(src, &src_i, &src_len, buf, &buf_len)){
			if(buf_len == 5 && memcmp(buf, "\\pard", 5) == 0){
				/* pard */
				write_lf(&state, &text);
				write_tag(&state, buf, buf_len);
				body = true;
			}else if(buf_len > 8 && memcmp(buf, "\\ansicpg", 8) == 0){
				/* ansicpgDD */
				int new_cp = 0;
				
				for(size_t i = 8; i < buf_len; ++ i){
					if(is_decimal(buf[i])){
						new_cp = new_cp * 10 + (buf[i] - '0');
					}else{
						new_cp = -1;
						break;
					}
				}
				if(new_cp >= 0){
					char cp_name[PAGE];
					
					sprintf(cp_name, "cp%d", new_cp);
					if(iconv_cd != NULL){
						iconv_close(iconv_cd);
					}
					iconv_cd = iconv_open(tocode, cp_name);
					if(iconv_cd == (iconv_t)-1){
						iconv_cd = NULL;
					}
				}
				if(iconv_cd == NULL){
					fprintf(stderr, "invalid tag: %.*s\n", (int)buf_len, buf);
				}
				write_tag(&state, buf, buf_len);
			}else{
				/* other tag */
				write_tag(&state, buf, buf_len);
			}
			continue;
		}
		/* escaped char */
		if(take_escaped_char(src, &src_i, &src_len, buf + 1)){
			if(buf[1] == '\n'){
				fputc('\\', stdout);
				state = TEXT;
				write_lf(&state, &text);
			}else{
				buf[0] = '\\';
				write_text(&state, &text, buf, 2);
			}
			continue;
		}
		/* '{' */
		if(take_char(src, &src_i, &src_len, '{')){
			write_lf(&state, &text);
			fputc('{', stdout);
			state = LPAREN;
			continue;
		}
		/* '}' */
		if(take_char(src, &src_i, &src_len, '}')){
			if(state == TEXT){
				write_lf(&state, &text);
			}
			fputc('}', stdout);
			state = RPAREN;
			continue;
		}
		/* lf */
		if(take_char(src, &src_i, &src_len, '\n')){
			continue;
		}
		/* other text */
		if(!body && (state == TAG || state == PARAM)){
			if(state == TAG && src[src_i] != ';'){
				fputc(' ', stdout);
			}
			fputc(src[src_i], stdout);
			state = PARAM;
		}else{
			write_text(&state, &text, src + src_i, 1);
		}
		++ src_i;
	}
	/* lf at last */
	write_lf(&state, &text);
	return 0;
}
