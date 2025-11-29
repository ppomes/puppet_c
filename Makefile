CC = gcc
LEX = flex
YACC = bison
CFLAGS = -Wall -Wextra -g -I./include -DYYDEBUG=1
LDFLAGS = -ly

# Ruby support (optional)
# Uncomment to enable Ruby/ERB support:
ENABLE_RUBY = 1

ifdef ENABLE_RUBY
    # Homebrew Ruby paths
    RUBY_PREFIX = /opt/homebrew/opt/ruby
    RUBY_CFLAGS = -I$(RUBY_PREFIX)/include/ruby-3.4.0 -I$(RUBY_PREFIX)/include/ruby-3.4.0/arm64-darwin25
    RUBY_LDFLAGS = -L$(RUBY_PREFIX)/lib -lruby.3.4
    CFLAGS += $(RUBY_CFLAGS) -DHAVE_RUBY
    LDFLAGS += $(RUBY_LDFLAGS)
endif

SRCDIR = src
OBJDIR = obj
BINDIR = bin
INCDIR = include

SOURCES = $(filter-out $(SRCDIR)/lex.yy.c $(SRCDIR)/puppet.tab.c, $(wildcard $(SRCDIR)/*.c))
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

LEX_SRC = $(SRCDIR)/puppet.l
YACC_SRC = $(SRCDIR)/puppet.y
LEX_OUT = $(SRCDIR)/lex.yy.c
YACC_OUT_C = $(SRCDIR)/puppet.tab.c
YACC_OUT_H = $(SRCDIR)/puppet.tab.h

PARSER_OBJS = $(OBJDIR)/lex.yy.o $(OBJDIR)/puppet.tab.o

TARGET = $(BINDIR)/puppetc

all: directories $(TARGET)

directories:
	@mkdir -p $(OBJDIR) $(BINDIR)

$(LEX_OUT): $(LEX_SRC)
	$(LEX) -o $@ $<

$(YACC_OUT_C) $(YACC_OUT_H): $(YACC_SRC)
	$(YACC) -d -o $(YACC_OUT_C) $<
	@cp $(SRCDIR)/puppet.tab.h $(INCDIR)/

$(OBJDIR)/lex.yy.o: $(LEX_OUT) $(YACC_OUT_H)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/puppet.tab.o: $(YACC_OUT_C)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(YACC_OUT_H)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(PARSER_OBJS) $(OBJECTS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(OBJDIR) $(BINDIR) $(LEX_OUT) $(YACC_OUT_C) $(INCDIR)/puppet.tab.h

test: $(TARGET)
	@echo "Running parser tests..."
	@mkdir -p tests/output
	@for file in tests/puppet/*.pp; do \
		echo "Testing $$file..."; \
		./$(TARGET) $$file > tests/output/$$(basename $$file).out 2>&1 || true; \
	done

.PHONY: all clean test directories