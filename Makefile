ALL = txf

all:	$(ALL)

txf: txf.c
	$(CC) -O2 -Wall $< -o $@

clean:
	$(RM) -f $(ALL)
