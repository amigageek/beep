CC             = m68k-amigaos-gcc
CFLAGS         = -Os -m68020 -noixemul -fomit-frame-pointer -mregparm=3 -msmall-code
LDFLAGS        = -s -lamiga
OUTDIR         = build
DEPFLAGS       = -MT $@ -MMD -MP -MF $(OUTDIR)/$*.Td

GENTABLES      = $(OUTDIR)/gentables
GENTABLES_SRCS = gencommon.c gentables.c
TABLES_HDR     = $(OUTDIR)/tables.h

GENIMAGES      = $(OUTDIR)/genimages
GENIMAGES_SRCS = gencommon.c genimages.c
IMAGES_HDR     = $(OUTDIR)/images.h

BEEP           = $(OUTDIR)/beep
BEEP_SRCS      = common.c exporter.c main.c model.c player.c synth.c synth.asm.s ui.c widgets.c
BEEP_OBJS      = $(patsubst %, $(OUTDIR)/%.o, $(basename $(BEEP_SRCS)))

$(shell mkdir -p $(OUTDIR) >/dev/null)

all: $(BEEP)

clean:
	rm -fr $(OUTDIR)

dist:
	cp -f $(BEEP) ..

$(BEEP): $(BEEP_OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

$(OUTDIR)/%.o : %.c $(OUTDIR)/%.d
	$(CC) $(DEPFLAGS) $(CFLAGS) -c -o $@ $<
	@mv -f $(OUTDIR)/$*.Td $(OUTDIR)/$*.d && touch $@

$(OUTDIR)/%.o : %.s
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/common.o: $(TABLES_HDR)
$(OUTDIR)/ui.o: $(IMAGES_HDR)

$(TABLES_HDR): $(GENTABLES)
	$(GENTABLES) > $@

$(IMAGES_HDR): $(GENIMAGES)
	$(GENIMAGES) > $@

$(GENTABLES): $(GENTABLES_SRCS)
	cc -o $@ $^ -lm

$(GENIMAGES): $(GENIMAGES_SRCS)
	cc -o $@ $^

$(OUTDIR)/%.d: ;

.PRECIOUS: $(OUTDIR)/%.d

include $(wildcard $(patsubst %, $(OUTDIR)/%.d, $(basename $(BEEP_SRCS))))
