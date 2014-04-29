
SRC_FILES= $(wildcard *.c)
OBJ_FILES= $(SRC_FILES:.c=.o)

# Chemin de ndes
NDES_ROOT_DIR=../ndes
INCL_DIR=$(NDES_ROOT_DIR)/include
LIB_DIR=$(NDES_ROOT_DIR)/lib

# Repertoires d'installation
BINDIR=$(HOME)/bin
CONFIGDIR=$(HOME)/PROGRAMMATION/C/GSE-SCHED-CAMPAIGNS/ConfigDir/

KNAPSACK = knapsack knapsack-meteo knapsack-meteo-ref
CONFIG_SRC = config

.PHONY: clean 

all : $(KNAPSACK)

knapsack : knapsack.o $(LIB_DIR)/libndes.a
	$(CC) knapsack.o -o knapsack -lm -L$(NDES_ROOT_DIR)/lib -lndes

knapsack-meteo : knapsack.c $(LIB_DIR)/libndes.a
	$(CC) $(CFLAGS) -I$(INCL_DIR) -DTEST_METEO knapsack.c -o knapsack-meteo -lm -L$(LIB_DIR) -lndes

knapsack-meteo-ref : knapsack.c $(LIB_DIR)/libndes.a
	$(CC) $(CFLAGS) -I$(INCL_DIR) -DTEST_METEO_REF knapsack.c -o knapsack-meteo-ref -lm -L$(LIB_DIR) -lndes

install : $(KNAPSACK)
	cp $(KNAPSACK) LancerCampagne.sh $(BINDIR)
	@mkdir $(CONFIGDIR) || true 
	cp $(CONFIG_SRC)/* $(CONFIGDIR)

clean :
	\rm -f *~ $(OBJ_FILES) $(KNAPSACK)

.c.o :
	$(CC) $(CFLAGS) -I$(INCL_DIR) $< -c

