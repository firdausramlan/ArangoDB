# -*- mode: Makefile; -*-

## -----------------------------------------------------------------------------
## --SECTION--                                                            PARSER
## -----------------------------------------------------------------------------

################################################################################
### @brief built sources
################################################################################

BUILT_SOURCES += $(BISON_FILES) $(BISONXX_FILES)

################################################################################
### @brief CLEANUP
################################################################################

if ENABLE_MAINTAINER_MODE 

CLEANUP += $(BISON_FILES) $(BISONXX_FILES)
endif

################################################################################
### @brief BISON
################################################################################

%.c: %.y
	@top_srcdir@/config/bison-c.sh $(BISON) $@ $<

################################################################################
### @brief BISON++
################################################################################

%.cpp: %.yy
	@top_srcdir@/config/bison-c++.sh $(BISON) $@ $<

## -----------------------------------------------------------------------------
## --SECTION--                                                       END-OF-FILE
## -----------------------------------------------------------------------------

## Local Variables:
## mode: outline-minor
## outline-regexp: "^\\(### @brief\\|## --SECTION--\\|# -\\*- \\)"
## End:
