#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include "error.h"
#include "tokenize.h"

/*
 * This is a basic expression tokenizer
 * it tokenizes things like numbers, string, and symbol literals
 */

token_t nulltok;

static token_t whitespace_tok;

static token_t quote_tok;

static  token_t left_paren = {
  PAREN,
    {
      "("
    }
};

static  token_t right_paren = {
  PAREN,
  {
  ")"
  }
};

static inline  char *
string_head(uint32_t n,
             char *in,
            char *out) {
  /* out must be large enough to store the number of characters
   * you want to select from in, plus a byte for the null terminator
   */
#ifndef NDEBUG
  size_t in_len = strlen(in);
#endif
  assert(n > 0 && n <= in_len);
  int iserror = snprintf(out, (size_t)n+1 , "%s", in);

  assert((iserror != -1) && ((size_t)iserror == in_len));

  if (iserror == -1) {
    printf("Out of memory");
    exit(EXIT_FAILURE);
  }
  return ( char*)out;
}

static inline token_t
make_token(token_val_t val,
           tok_t toktype) {
  token_t result;
  result.token_type = toktype;
  result.token = val;
  return result;
}

token_t
testfunc(void) {
  token_val_t wspace;
  wspace.whitespace = true;
  return make_token(wspace, QUOTE);
}


bool
push_token(token_stream *tokens,
           token_t token) {
  /*
   * Check if tokens points to NULL
   */

  size_t len;
  size_t max;

  CHECK(tokens);

  len = tokens->length;
  max = tokens->max_length;

  assert(len <= max);
  assert(max > 0);

  if (len == max) {
    /* We've reached the maximum stack size
     * So we must try to increase that by GROWTH_SIZE
     */
    token_t *new_tokens = (token_t *)realloc(tokens->tokens, sizeof (token_t) * (max + GROWTH_SIZE));
    if (!new_tokens) {
      printf("Could not allocate enough memory for the token stack\n");
      exit(EXIT_FAILURE);
    }
    tokens->tokens = new_tokens;
    tokens->max_length = max + GROWTH_SIZE;
    tokens->tokens[len] = token;
    tokens->length++;
    return true;
  }
  tokens->tokens[len] = token;
  tokens->length++;
  return true;
}

bool
pop_token(token_stream *tokens) {
  size_t len;
  CHECK(tokens);

  len = tokens->length;

  assert(len != 0);
  len--;
  CHECK(tokens->tokens);

  tokens->length--;
  return true;
}

inline token_t
peek_token(token_stream *tokens) {
  /*
   * Check if tokens points to NULL
   */
  size_t len = tokens->length;
  size_t max = tokens->max_length;
  CHECK(tokens);
  assert(len != 0);

  if (len == 0 || len > max) {
    return nulltok;
  }
  return tokens->tokens[len-1];
}

static inline uint32_t
match_int(source_t source,
          uint32_t begin,
           uint32_t length) {
  /* Return false if there is no match
   * otherwise return the position of the end of the match + 1
   */
  uint32_t i = begin;
  uint32_t test;
  CHECK(source);
  assert(length > 0);

  if (source[i] == '+' ||
      source[i] == '-') {
    i++;
  }
  test = i;
  while (i < length &&
         isdigit(source[i])) {
    i++;
  }
  if (i == test)
    return false;
  return i;
}

static inline uint32_t
match_float(source_t source,
            uint32_t begin,
             uint32_t length) {
  /* Return false if there is no match
   * otherwise:
   *  if there is a leading decimal point and then a valid int match:
   *    return the position of the end of the match
   *  if there is a leading valid int match:
   *    but no decimal point match after that:
   *      return false
   *    if there is a decimal point match and then a valid int match:
   *        return the position of the match
   *    if there is no valid int match:
   *      return false
   * ALWAYS returns the position + 1 to avoid confusion with false (which is a valid index)
   */
  uint32_t i, leading_int_match, trailing_int_match;
  CHECK(source);
  assert(length > 0);

  i = begin;
  leading_int_match = match_int(source, i, length);

  if (leading_int_match) {
    i = leading_int_match;
  }

  assert(i <= length);

  if (source[i] != '.' ||
      source[i] == '+' ||
      source[i] == '-') {
    if (((i+1) <= length) && /* Make sure there is at least two characters to look at */
        ((source[i] == '+') ||
         (source[i] == '-'))
        && (source[i+1] == '.')) {
      i++;
    }
    else {
      return false;
    }
  }
  i++;

  trailing_int_match = match_int(source, i, length);
  if (trailing_int_match) {
    return trailing_int_match;
  }
  return false;
}

static inline uint32_t
match_identifier(source_t source,
                 uint32_t begin,
                  uint32_t length) {

  /* Return false if there is no match
   *    if there is a match for any characters that are not:
   *      whitespace
   *      a parenthesis ( )
   *      a brace { }
   *      a square bracket [ ]
   *        then return the position of the match + 1
   *    if there is nothing else to match:
   *      return false
   */
  uint32_t i = begin;
  CHECK(source);
  assert(length > 0);
  if (source[i] == '*' ||
      source[i] == '+' ||
      source[i] == '-' ||
      source[i] == '/') {
    return i+1;
  }

  while (i < length &&
         !(source[i] == '(' ||
           source[i] == ')' ||
           isspace(source[i]))) {
    i++;
  }

  if (i == begin) {
    return false;
  }
  assert(i <= length);
  return i;
}

static inline uint32_t
match_symbol(source_t source,
             uint32_t begin,
              uint32_t length) {
  uint32_t i;
  CHECK(source);
  assert(length > 0);

  i = begin;
  if (source[i] != '\'') {
    return false;
  }
  i++;
  while ((isspace(source[i]) ||
         (source[i] == '\'')) && i < length) { /* consume leading whitespace and quotes */
    i++;
  }
  if (source[i] == ')') {
    printf("Unexpected )\n");
    exit(EXIT_FAILURE);
  }
  if (source[i] == '(') {
   return i;
  }
  while (!isspace(source[i]) &&
         source[i] != '(' &&
         source[i] != ')' &&
         i < length) {
    i++;
  }
  if (i == begin+1) { /* if we did not increment i more than once (before the loop) */
    return false;
  }
  return i;
}

static inline void
extract_token(uint32_t position,
              uint32_t begin,
               source_t source,
               char *token_val) {
    assert(position > begin);
    string_head(position - begin,
                &source[begin],
                (char *)token_val);
}

token_stream
tokenize(source_t source,
         uint32_t begin,
         uint32_t length) {
  /*
   * Remember to free everything from this struct
   * for example, token_stack.tokens will not necessarily be
   * equal to tokens after this function has run
   *
   */
  uint32_t position = begin;
  char *current_token_val;
  token_stream token_stack;
  token_val_t current_token;
  token_t *tokens = (token_t *)calloc(STACK_SIZE, sizeof (token_t));

  assert(begin == 0);
  assert(length > 0);
  CHECK(source);

  token_stack.length = 0;
  token_stack.max_length = STACK_SIZE;
  token_stack.tokens = tokens;
  char lookahead = '\0';
  assert(STACK_SIZE > 0);

  while (begin <= length && source[begin]) {
    if (source[begin] == '(') {
      /*Matched a left paren */
      position = begin + 1;
      push_token(&token_stack, left_paren);
    }
    else if (source[begin] == ')') {
      /*Matched a left paren */
      position = begin + 1;
      push_token(&token_stack, right_paren);
    }
    else if (isspace(source[begin])) {
      position = begin + 1;
      push_token(&token_stack, whitespace_tok);
      /* Matched a whitespace character */
    }
    else if ((position = match_float(source, begin, length))) {
      /* Matched a float */
      lookahead = source[position];
      source[position] = '\0';
      source[position] = lookahead;
      assert(position > begin);
      current_token_val = (char *)calloc(((position - begin) + 1), sizeof (char));
      CHECK(current_token_val);
      extract_token(position, begin, source, current_token_val);
      current_token.floating = current_token_val;
      push_token(&token_stack, make_token(current_token, FLOATING));
    }
    else if ((position = match_int(source, begin, length))) {
      /* Matched an int */
      lookahead = source[position];
      source[position] = '\0';
      assert(position > begin);
      assert(position <= length);

      source[position] = lookahead;
      current_token_val = (char *)calloc(((position - begin) + 1), sizeof (char));
      CHECK(current_token_val);
      extract_token(position, begin, source, current_token_val);
      current_token.integer = current_token_val;
      push_token(&token_stack, make_token(current_token, INTEGER));
    }
    else if ((position = match_symbol(source, begin, length))) {
      /* Matched a symbol */
      lookahead = source[position];
      source[position] = '\0';
      assert(position > begin);
      assert(position <= length);

      source[position] = lookahead;
      current_token_val = (char *)calloc(((position - begin) + 1), sizeof (char));
      CHECK(current_token_val);
      extract_token(position, begin, source, current_token_val);
      current_token.symbol = current_token_val;
      push_token(&token_stack, make_token(current_token, SYMBOL));
    }
    else if (source[begin] == '\'') {
      /* Matched a quote (apostrophe) */
      position = begin + 1;
      push_token(&token_stack, quote_tok);
    }
    else if ((position = match_identifier(source, begin, length))) {
      /* Matched an identifier */
      lookahead = source[position];
      source[position] = '\0';
      assert(position > begin);
      assert(position <= length);

      source[position] = lookahead;
      current_token_val = (char *)calloc(((position - begin) + 1), sizeof (char));
      CHECK(current_token_val);
      extract_token(position, begin, source, current_token_val);
      current_token.identifier = current_token_val;
      push_token(&token_stack, make_token(current_token, IDENTIFIER));
      /* Matched an identifier */
    }
    else if (position <= begin) {
      printf("Source is too large to read\n");
      exit(EXIT_FAILURE);
    }
    else {
      printf("Unmatched token\n");
      exit(EXIT_FAILURE);
    }
    begin = position;
  }

  return token_stack;
}

int
free_token( void *key,
            void *val) {
  /* silence warnings about unused parameters, key and val point to the same data*/
  (void)key;
  free((char *)val);
  return true;
}

bool
release_tokens(token_stream *tokens) {
  /* Iterate through the stack, release each token
   * Then release the entire stack
   */
  CHECK(tokens);
  CHECK(tokens->tokens);
  assert(tokens->max_length > 0);
  free(tokens->tokens);

  return true;
}

#ifndef LIB
int main(void) {
  nulltok.token_type = EMPTY;
  nulltok.token.null_token = false;

  whitespace_tok.token_type = WSPACE;
  whitespace_tok.token.whitespace= false;

  quote_tok.token_type = QUOTE;
  quote_tok.token.quote= false;

  void *source_code = malloc(111000);
  uint32_t nbytes = read(STDIN_FILENO, source_code, 111000);
  if (nbytes == 0) {
    exit(EXIT_FAILURE);
  }
  token_stream toks = tokenize((source_t)source_code, 0, nbytes);
  token_t current_tok;
  while (toks.length > 0) {
    current_tok = peek_token(&toks);
    switch (current_tok.token_type) {
      case SYMBOL:
        printf("symbol: %s\n", current_tok.token.symbol);
        break;
      case IDENTIFIER:
        printf("identifer: %s\n", current_tok.token.identifier);
        break;
      case INTEGER:
        printf("integer: %s\n", current_tok.token.integer);
        break;
      case FLOATING:
        printf("floating: %s\n", current_tok.token.floating);
        break;
      case QUOTE:
        printf("quote: '\n");
        break;
      case WSPACE:
        printf("whitespace\n");
        break;
      case PAREN:
        printf("paren: %s\n", current_tok.token.parenthesis);
        break;
      case EMPTY:
        printf("this should not be empty\n");
        break;
      case STRING:
        printf("string: %s\n", current_tok.token.string);
        break;
      default:
        printf("oops, there was an unknown token, check valgrind or gdb\n");
    }
    pop_token(&toks);
  }
  release_tokens(&toks);
  return 0;
}
#endif
