/*
 * A faire
 *
 *   . Une sonde par MODCOD sur le nombre d'octets transmis par BBFRAME
 *     (dans dvb_s2)
 *   . Construire dynamiquement les tableaux, c'est plus propre !
 * 
 * Fait
 *   . Temps d'émission d'une BBFRAME (idem)
 *   . Un générateur discret de valeurs entières pour la taille
 *     des paquets
 */

//#define DEBUG_EXHAUSTIVE_VERSION 1
//#define TEST_METEO 1
//#define TEST_METEO_REF 1    // Memes MODCODs que meteo mais pas chgt
//#define TEST_INCIVISME 1
//#define TEST_MAAIKE 1

#define OUTPUT_PNG

/*======================================================================*/
/*                             HISTORIQUE                               */
/*======================================================================*/
/*
#define KS_VERSION  1   // Première utilisation du numero de version
#define KS_VERSION  2   // 2012-03-12 definition de la deriveUtility
			// dans un .h pour éviter un comportement
			// anormal de l'aglo 2
#define KS_VERSION  3   // 2012-03-13 Saisie de la taille de la file
			// et du log des pertes. Suspiscion de bug
			// dans le décompte des etats en
			// exhaustif. Evaluation des pertes par une
			// moyenne temporelle
#define KS_VERSION  4   // 2012-03-13 définition d'une taille de file
			// minimale
                        // Ajout du mode TEST_METEO_REF (cf ci dessus)
#define KS_VERSION  5   // 2013-04-07 la sonde sur les pertes mesure
                        // un débit et non une moyenne (la moyenne de
                        // la taille des paquets perdus est sans intérêt)
#define KS_VERSION  6   // 2013-09-06 ajout de l'algo ALGO_UTILITY_PROP
#define KS_VERSION  7   // 2013-12-12 génération de PNG via gnuplot
#define KS_VERSION  8   // 2013-12-15 Utilisation d'une durée négative
                        // pour changer le mode de saisie (à virer dès
                        // que possible mais permet la compatibilité
                        // ascendante avec mes scripts de lancement
                        // de campagne).
#define KS_VERSION  9   // 2014-01-30 Ajout d'un traitement par lot
#define KS_VERSION 10   // 2014-02-10 Ajout dalgorithme par lot
			// (ALGO_BATCH_UTIL)
			*/
#define KS_VERSION 11   // 2104-02-12 Ajout de variantes sur le batch
/*======================================================================*/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>    // Malloc, NULL, exit, ...
#include <assert.h>
#include <strings.h>   // bzero, bcopy, ...
#include <stdio.h>     // printf, ...
#include <string.h>
#include <time.h>      // time
#include <unistd.h>    // close
#include <math.h>      // ceil

#include <event.h>
#include <file_pdu.h>
#include <pdu-source.h>
#include <pdu-sink.h>
#include <date-generator.h>
#include <probe.h>
#include <schedUtility.h>
#include <schedACMBatch.h>
#include <sched_ks.h>
#include <dvb-s2-ll.h>

// Utilisation de sondes exhaustives pour toutes les mesures (lourd !!)
//#define USE_EXHAUSTIVE_PROBES

// Affichage des résultats de chaque simulation
#define AFFICHE_SIMU

/*
 * Structure permettant de définir la modification des
 * caractéristiques d'un MODCOD
 *
 * Attention, tant que ce n'est pas intégré dans l'ordonnanceur, si
 * deux MODCODs se retrouvent avec les mêmes caractéristiques, il faut
 * les fusionner, de sorte à pouvoir transporter dans la même BBFRAME
 * des paquets émis vers les deux MODCODs d'origine
 */
struct modifMODCOD {
   int mc ;          // Quel MC ?
   int bitLength;    // Nouvelle taille de charge utile
   int bitPerSymbol; // Nouvelle valence
   struct DVBS2ll_t * dvbs2ll;
};

/*======================================================================*/
/*   Tous les paramètres de base de la simulation sont ici en variables */
/* globales. Oui, je sais, ...                                          */
/*======================================================================*/

/*----------------------------------------------------------------------*/
/*   Caractéristiques du lien                                           */
/*----------------------------------------------------------------------*/
#define SYMBOL_PER_SEC 10000000

#define NB_MODCOD 4

/*----------------------------------------------------------------------*/
/*   Caractéristiques de base des MODCOD                                */
/*----------------------------------------------------------------------*/

int chargeUtileMC[NB_MODCOD] = {
#if defined(DEBUG_EXHAUSTIVE_VERSION) || defined(TEST_MAAIKE)
   C13SIZE,
   C13SIZE, 
   C13SIZE,
   C13SIZE
#elif defined(TEST_METEO)||defined(TEST_METEO_REF)
   C13SIZE,
   C25SIZE,
   C34SIZE, // Passera a C25SIZE
   C56SIZE   
#else   // Le jeu standard jusqu'a present
   C23SIZE,
   C23SIZE,
   C34SIZE,
   C56SIZE   
#endif
};


int valenceMC[NB_MODCOD] = {
#if defined(TEST_MAAIKE)
   MQPSK,
   MQPSK,
   MQPSK,
   MQPSK
#else   // Le jeu standard jusqu'a present
   MQPSK,
   M8PSK,
   M16APSK,
   M16APSK
#endif
};

/************************************************************************/
/*   Les événements météorologiques modélisés ci-dessous ne sont pas    */
/* forcément très réalistes. Ils ont pour conséquence le changement     */
/* d'un modcod dans sa globalité. Il vaudrait mieux changer une partie  */
/* trafic depuis un modcod vers un autre, mais comment mesurer les perf?*/
/************************************************************************/

/*----------------------------------------------------------------------*/
/*   Description des événements météorologique.                         */
/*----------------------------------------------------------------------*/
typedef struct {
   struct DVBS2ll_t * dvbs2ll;
   int mc;
   int chargeUtile;
   int valence;
} meteoEvent_t;

/*----------------------------------------------------------------------*/
/*   Simulation d'un événement météorologique.                           */
/*----------------------------------------------------------------------*/
void changementMeteo(void * m)
{
   meteoEvent_t * me = (meteoEvent_t *)m;

   DVBS2ll_setModcod(me->dvbs2ll, me->mc, me->chargeUtile, me->valence);
}

/*----------------------------------------------------------------------*/
/*   Création et programmation d'un événement météo.                    */
/*----------------------------------------------------------------------*/
void scheduleMeteoEvent(double date, struct DVBS2ll_t * dvbs2ll, int mc, int chargeUtile, int valence)
{
   meteoEvent_t * me = (meteoEvent_t *)malloc(sizeof(meteoEvent_t));

   me->dvbs2ll = dvbs2ll;
   me->mc = mc;
   me->chargeUtile = chargeUtile;
   me->valence = valence;

   event_add(changementMeteo, (void *)me, date);
}

/************************************************************************/
/*   Simulation de l'inciisme d'un flot. Ici aussi, c'est assez primitif*/
/* On va appliqué un coefficient multiplicateur sur le débit (via le    */
/* lambda).                                                             */
/************************************************************************/
/*----------------------------------------------------------------------*/
/*   Description des événements de changement de comportement.          */
/*----------------------------------------------------------------------*/
typedef struct {
   struct dateGenerator_t   * dateGen;
   double lambda;
} changeEvent_t;

/*----------------------------------------------------------------------*/
/*   Simulation d'un événement de changement de comportement.           */
/*----------------------------------------------------------------------*/
void changementComportement(void * m)
{
   changeEvent_t * ce = (changeEvent_t *)m;

   dateGenerator_setLambda(ce->dateGen, ce->lambda);
}

/*----------------------------------------------------------------------*/
/*   Création et programmation d'un événement  de changement de         */
/* comportement                                                         */
/*----------------------------------------------------------------------*/
void scheduleChangementComportement(double date, struct dateGenerator_t * dateGen, double lambda)
{
   changeEvent_t * ce = (changeEvent_t *)malloc(sizeof(changeEvent_t));

   ce->lambda = lambda;
   ce->dateGen = dateGen;

   event_add(changementComportement, (void *)ce, date);
}

/************************************************************************/

/*----------------------------------------------------------------------*/
/*   Caractéristiques de la simulation                                  */
/*----------------------------------------------------------------------*/
double dureeSimulation = 50.0;

#define NB_SIMULATIONS 1

/*----------------------------------------------------------------------*/
/*   Caractéristiques de l'ordonnanceur                                 */
/*----------------------------------------------------------------------*/
#define NB_QOS_LEVEL 3

/*----------------------------------------------------------------------*/
/*   Caractéristiques des sources                                       */
/*----------------------------------------------------------------------*/

double chargeEntree = 0.8;

/*
 *  Génération des tailles de paquet (un seul jeu de paramètres pour
 *  toutes les sources)
 */

#if defined(DEBUG_EXHAUSTIVE_VERSION)|| defined(TEST_MAAIKE)
unsigned int packetSize [] = {
  669,
  669,
  669,
  669
};
#else
// Tableau des tailles de paquets
unsigned int packetSize [] = {
   128,
   256,
   512,
   1024
};
#endif 

// Tableau des probabilités associées
double packetSizeProba [] = {
   0.25,
   0.25,
   0.25,
   0.25
};

// Tableau des lambda des sources
double lambda[NB_MODCOD][NB_QOS_LEVEL] = {
  {0.0, 0.0, 0.0},
  {0.0, 0.0, 0.0},
  {0.0, 0.0, 0.0},
  {0.0, 0.0, 0.0}
};

// Pondération (normalisée) de la charge sur les MODCODs. C'est ce jeu
// de coefficients qui constitue le scenario
double alphaMC[NB_MODCOD];
int scenario = 0;

double alphaScenario[5][NB_MODCOD]= {
  {0.25, 0.25, 0.25, 0.25},      // Scenario 0 : uniform
  {0.01, 0.19, 0.30, 0.50},      // Scenario 1 : temps clair
  {0.50, 0.30, 0.19, 0.01},      // Scenario 2 : inverse
#if defined(TEST_METEO)||defined(TEST_METEO_REF)
  {0.00, 0.00, 1.00, 0.00},      // Scenario 3 : un seul modcod
#else
  {0.00, 0.00, 0.00, 1.00},      // Scenario 3 : un seul modcod
#endif
  {0.50, 0.50, 0.00, 0.00}       // Scenario 4 : test de Maaike
};

// Répartition (absolue) de la charge sur les MODCODs
double rho[NB_MODCOD];

// Répartition (normalisée) de la charge d'un MODCOD sur les files 
double wghtQoS[NB_QOS_LEVEL] = {
   0.2,
   0.3,
   0.5
};

// Le type de QoS de chaque file et son éventuel paramètre
int typeQoS[NB_MODCOD][NB_QOS_LEVEL];

double betaQoS[NB_MODCOD][NB_QOS_LEVEL];

/*----------------------------------------------------------------------*/
/*   Les variables suivantes sont initialisées dans le programme        */
/*----------------------------------------------------------------------*/
double debitBinaire[NB_MODCOD];// Débit max offert par chaque MODCOD en bit/s
double taillePaquet[NB_MODCOD][NB_QOS_LEVEL]; // Taille moyenne théorique

/*----------------------------------------------------------------------*/
/*   Initialisation des principaux paramètres.                          */
/*----------------------------------------------------------------------*/
void initParametres()
{
   int m;

   for (m = 0; m < NB_MODCOD; m++) {
     printf("%f ", alphaScenario[scenario][m]);
   }

   for (m = 0; m < NB_MODCOD; m++) {
     printf("%f ", alphaScenario[m][scenario]);
   }

   for (m = 0; m < NB_MODCOD; m++) {
      alphaMC[m] =  alphaScenario[scenario][m];
      rho[m] = chargeEntree * alphaMC[m];
   }
}

/*----------------------------------------------------------------------*/
/*                                                                      */
/*----------------------------------------------------------------------*/
int main() {
   // Construction des sources
   struct PDUSource_t       * sourcePDU[NB_MODCOD][NB_QOS_LEVEL];
   struct dateGenerator_t   * dateGen[NB_MODCOD][NB_QOS_LEVEL];
   struct randomGenerator_t * sizeGen[NB_MODCOD][NB_QOS_LEVEL];

   struct filePDU_t   * files[NB_MODCOD][NB_QOS_LEVEL];
/*----------------------------------------------------------------------*/
/*   La taille d'une file est exprimée en durée (en seconde) au débit de*/
/* la source. O pour "no limit".                                        */
/*----------------------------------------------------------------------*/
   double fileDuration = 0.0;

   struct DVBS2ll_t   * dvbs2ll;
   struct schedACM_t  * schedks;

   // La fin des BBFRAMEs
   struct PDUSink_t  * sink;

   /* Les sondes */
   struct probe_t     * sps[NB_MODCOD][NB_QOS_LEVEL]; // Tailles des paquets des sources
   struct probe_t     * tms[NB_MODCOD][NB_QOS_LEVEL]; // Temps moyen de séjour dans la file
   struct probe_t     * sip; // Sink Input Probe
   struct probe_t     * bbfps[NB_MODCOD]; // BBFRAME payload size
   struct probe_t     * df; // Combien de DUMMY frames ?
   struct probe_t     * pqinmc[NB_MODCOD][NB_QOS_LEVEL][NB_MODCOD]; // Nombre de pq de (i, j) dans k

   int dumpThroughput = 0;
   struct probe_t     * bwProbe[NB_MODCOD][NB_QOS_LEVEL]; // Le débit *estimé par le scheduler*
   struct probe_t     * actualBwProbe[NB_MODCOD][NB_QOS_LEVEL]; // Le débit "réel" (mesuré par une sonde a fenetre)

   // Les deux suivantes sont pour le moment inutilisées car apparemment buggées ...
   //   struct probe_t     * emaBwProbe[NB_MODCOD][NB_QOS_LEVEL]; // Le débit "réel" (mesuré par une sonde EMA)
   //   struct probe_t     * periodicEmaBwProbe[NB_MODCOD][NB_QOS_LEVEL]; // Le débit "réel" (prélevé périodiquement sur la sonde EMA)

   // Les sondes permettant de faire des moyennes sur les simu
   struct probe_t     * nbPaquetsMoyen[NB_MODCOD][NB_QOS_LEVEL];
   struct probe_t     * bbfpsMoyen[NB_MODCOD]; // BBFRAME payload size
   struct probe_t     * tmsMoyen[NB_MODCOD][NB_QOS_LEVEL];

   struct probe_t     * tmsGlobal; // Sonde sur le temps de service moyen global

   int dumpDropProbe = 1;
   struct probe_t     * pertes[NB_MODCOD][NB_QOS_LEVEL];

   struct probe_t     * nbCas; // Une sonde sur le nombre de cas testés

   // Quel algorithme ?
#define ALGO_KS           0
#define ALGO_UTILITY_PROP 1    // Rendu caduque par le 4 avec une durée d'époque nulle
#define ALGO_UTILITY      2
#define ALGO_KS_EXHAUSTIF 3
#define ALGO_UTIL_PROP_BATCH 4
#define ALGO_BATCH_UTIL      5
#define ALGO_BATCH_LENGTH    6
#define ALGO_BATCH_UTIL_LENGTH      7

   int algorithme ;

   // Fait-on du déclassement ?
   char declString[32];
   int declassement;

   int mc;
   int q, m;

   int ns;
   char name[64];
   double debit;

   double somme;

   /* Sortie des résultats */
   char filename[512], dataFilename[512], gnuplotFilename[512], logFilename[512];
   FILE * fichierData, * fichierGnuplot, * fichierLog;

   // Fichier de débit
   int fdDebit;

   double max;

   // Pour les statistiques
   int nbPqTotal, nbPq;
   double volumeTotal;

   time_t dateDebut, dateFin;

   // Durée d'une époque en cas d'ordonnanceur par lot.
   double epochMinDuration = 0.016;
   double minFrameDuration = 0.0;
   int sequenceMaxLength = 1;
   struct probe_t * epochDurationProbe;

/*----------------------------------------------------------------------*/
/*   Saisie des paramètres.                                             */
/*----------------------------------------------------------------------*/

/* Liste des paramètes à initialiser

   dureeSimulation
   chargeEntree

 */

   printf("Duree de la simulation (en secondes) = ");
   fflush(stdout);
   scanf("%lf", &dureeSimulation);

   /* On va utiliser une valeur négative de durée pour basculer vers un 
      nouveau mode de saisie */
   if (dureeSimulation < 0) {
/*----------------------------------------------------------------------*/
/*   Nouveau mode de saisie, en utilisant au max des fichiers           */
/*   EN COURS DE REALISATION                                            */
/*   Soucis pour le moment : comment éviter de se retrouver avec une    */
/*  kyrielle de fichiers ? Des fichiers par défaut ?                    */
/*----------------------------------------------------------------------*/
     printf("Are you kidding me ?!\n");
     exit(1);

   } else {
/*----------------------------------------------------------------------*/
/* Ancien mode de saisie : tout au clavier                              */
/*----------------------------------------------------------------------*/
      printf("Charge en entree       = ");
      fflush(stdout);
      scanf("%lf", &chargeEntree);

      printf("Scenario (0:eql,1:clr,2:inv,3:1mc) = ");
      fflush(stdout);
      scanf("%d", &scenario);

      printf("Algo (%d=KS, %d=propUtil, %d=util, %d=ks exhau, %d=batch util) = ", ALGO_KS, ALGO_UTILITY_PROP, ALGO_UTILITY, ALGO_KS_EXHAUSTIF, ALGO_BATCH_UTIL);
   fflush(stdout);
   scanf("%d", &algorithme);
   printf("Durée d'époque (si par lot, 0.0 sinon) = ");
   fflush(stdout);
   scanf("%lf", &epochMinDuration);


   for (q = 0; q < NB_QOS_LEVEL; q++) {
     printf("File %d : quelle QoS ? (1=log/2=lin/3=exp/4=exn/5=PQ/6=BT/7=BB) : ", q);
      fflush(stdout);
      scanf("%d", &typeQoS[0][q]);
      for (m = 1; m < NB_MODCOD; m++) {
         typeQoS[m][q] = typeQoS[0][q];
      }
   }

   printf("Declassement (TRUE/FALSE) ? = ");
   fflush(stdout);
   scanf("%s", declString);
   if (!strcmp(declString, "TRUE")) {
      declassement = 1;
   } else  if (!strcmp(declString, "FALSE")) {
      declassement = 0;
   } else {
      printf("*** ERREUR de saisie\n");
      exit(1);
   }
   // Répartition de la charge entre les files d'un même MODCOD
   somme = 0.0;
   for (q = 0; q < NB_QOS_LEVEL; q++) {
      printf("File %d : quel poids ? : ", q);
      fflush(stdout);
      scanf("%lf", &wghtQoS[q]);
      somme += wghtQoS[q];
   }
   // On normalise
   for (q = 0; q < NB_QOS_LEVEL; q++) {
      wghtQoS[q] = wghtQoS[q]/somme;
   }

   // Paramètres des fonctions de mesure de QoS
   for (q = 0; q < NB_QOS_LEVEL; q++) {
      printf("File %d : quel QoS param ? : ", q);
      fflush(stdout);
      scanf("%lf", &betaQoS[0][q]);
      somme += betaQoS[0][q];
      for (m = 1; m < NB_MODCOD; m++) {
         betaQoS[m][q] = betaQoS[0][q];
      }
#ifdef TEST_MAAIKE
         betaQoS[0][q] = 3200.0*betaQoS[0][q];
#endif
   }

   // Nom du fichier de sortie
   printf("Nom du fichier : ");
   fflush(stdout);
   scanf("%s\n", filename);


   printf("Fichiers de debit (TRUE/FALSE) ? = ");
   fflush(stdout);
   scanf("%s\n", declString);
   if (!strcmp(declString, "TRUE")) {
      dumpThroughput = 1;
   } else  if (!strcmp(declString, "FALSE")) {
      dumpThroughput = 0;
   } else {
      printf("*** ERREUR de saisie sur log debits\n");
      exit(1);
   }


   printf("Fichiers de pertes (TRUE/FALSE) ? = ");
   fflush(stdout);
   scanf("%s\n", declString);
   if (!strcmp(declString, "TRUE")) {
      dumpDropProbe = 1;
   } else  if (!strcmp(declString, "FALSE")) {
      dumpDropProbe = 0;
   } else {
      printf("*** ERREUR de saisie sur log pertes\n");
      exit(1);
   }

   printf("Capacite de chaque file (en secondes) = ");
   fflush(stdout);
   scanf("%lf", &fileDuration);
   } // if dureeSimulation < 0 

   // Ouverture des fichiers
   printf("Ouverture des fichiers de log ...\n");
   sprintf(dataFilename, "%s.data", filename);
   sprintf(gnuplotFilename, "%s.gnu", filename);
   sprintf(logFilename, "%s.log", filename);
   fichierData = fopen(dataFilename, "w");
   fichierGnuplot = fopen(gnuplotFilename, "w");
   fichierLog = fopen(logFilename, "w");

/*----------------------------------------------------------------------*/
/*   Initialisation de la simulation.                                   */
/*----------------------------------------------------------------------*/

   /* Creation du simulateur */
   printf("Creation du simulateur\n");
   motSim_create();

   // On ne s'intéresse pas aux paquets aprés transmission
   sink = PDUSink_create();

   // On va mesurer le débit de sortie
#ifdef USE_EXHAUSTIVE_PROBES
   sip = probe_createExhaustive();
#else
   sip = probe_createMean();
#endif
   sprintf(name, "sip");
   probe_setName(sip, name);

   PDUSink_addInputProbe(sink, sip);

/*----------------------------------------------------------------------*/
/* Création de la couche 2 DVB-S2                                       */
/*----------------------------------------------------------------------*/
   dvbs2ll = DVBS2ll_create(sink, (processPDU_t)PDUSink_processPDU,
                            SYMBOL_PER_SEC, FEC_FRAME_BITSIZE_LARGE);

   // La sonde des DUMMY
#ifdef USE_EXHAUSTIVE_PROBES
   df= probe_createExhaustive();
#else
   df= probe_createMean();
#endif
   DVBS2ll_addDummyFecFrameProbe(dvbs2ll, df);

   // Ajout des MODCODs
   for (mc = 0; mc < NB_MODCOD; mc++){
      DVBS2ll_addModcod(dvbs2ll, chargeUtileMC[mc], valenceMC[mc]);
      debitBinaire[mc] = (double)DVBS2ll_bbframePayloadBitSize(dvbs2ll, mc)/DVBS2ll_bbframeTransmissionTime(dvbs2ll, mc);
   };

   // Pour les ordonnanceurs par lot
   minFrameDuration = DVBS2ll_bbframeTransmissionTime(dvbs2ll, 0);
   for (m = 1; m < DVBS2ll_nbModcod(dvbs2ll); m++) {
      minFrameDuration =  (DVBS2ll_bbframeTransmissionTime(dvbs2ll, m) < sequenceMaxLength)?DVBS2ll_bbframeTransmissionTime(dvbs2ll, m):sequenceMaxLength;
   }
   sequenceMaxLength = (epochMinDuration/minFrameDuration > 1.0) ? (int)ceil(epochMinDuration/minFrameDuration):1;

   /* Création de l'ordonnanceur */
   switch (algorithme) {
      case ALGO_KS :
	schedks = sched_kse_create(dvbs2ll, NB_QOS_LEVEL, declassement, 0);
      break;
      case ALGO_KS_EXHAUSTIF :
	schedks = sched_kse_create(dvbs2ll, NB_QOS_LEVEL, declassement, 1);
      break;
      case ALGO_UTILITY :
         schedks = schedUtility_create(dvbs2ll, NB_QOS_LEVEL, declassement);
      break;
      case ALGO_UTILITY_PROP :
         schedks = schedUtilityProp_create(dvbs2ll, NB_QOS_LEVEL, declassement);
      break;
      case ALGO_UTIL_PROP_BATCH :
        schedks = schedUtilityPropBatch_create(dvbs2ll, NB_QOS_LEVEL, declassement, sequenceMaxLength);
	schedACM_setEpochMinDuration(schedks, epochMinDuration);
        epochDurationProbe = probe_createMean();
        schedACM_addEpochTimeDurationProbe(schedks, epochDurationProbe);
      break;
      case ALGO_BATCH_UTIL :
        schedks = schedACMBatch_create(dvbs2ll, NB_QOS_LEVEL, declassement, sequenceMaxLength, schedBatchModeUtil);
	schedACM_setEpochMinDuration(schedks, epochMinDuration);
        epochDurationProbe = probe_createMean();
        schedACM_addEpochTimeDurationProbe(schedks, epochDurationProbe);
      break;
      case ALGO_BATCH_LENGTH :
        schedks = schedACMBatch_create(dvbs2ll, NB_QOS_LEVEL, declassement, sequenceMaxLength, schedBatchModeLength);
	schedACM_setEpochMinDuration(schedks, epochMinDuration);
        epochDurationProbe = probe_createMean();
        schedACM_addEpochTimeDurationProbe(schedks, epochDurationProbe);
      break;
      case ALGO_BATCH_UTIL_LENGTH :
        schedks = schedACMBatch_create(dvbs2ll, NB_QOS_LEVEL, declassement, sequenceMaxLength, schedBatchModeUtilThenLength);
	schedACM_setEpochMinDuration(schedks, epochMinDuration);
        epochDurationProbe = probe_createMean();
        schedACM_addEpochTimeDurationProbe(schedks, epochDurationProbe);
      break;
      default :
         printf("Algo %d inconnu !\n", algorithme);
         exit(12);
      break;
   }

   nbCas = probe_createMean();
   schedACM_addNbSolProbe(schedks, nbCas);

/*----------------------------------------------------------------------*/
/* Creation des files et des sources.                                   */
/*----------------------------------------------------------------------*/
   initParametres();


#ifdef USE_EXHAUSTIVE_PROBES
   tmsGlobal = probe_createExhaustive();
#else
   tmsGlobal = probe_createTimeSliceAverage(0.5);
#endif
   sprintf(name, "tmsGlobal");
   probe_setName(tmsGlobal, name);

   for (m = 0; m < NB_MODCOD; m++) {
#ifdef USE_EXHAUSTIVE_PROBES
      bbfps[m] = probe_createExhaustive();
#else
      bbfps[m] = probe_createMean();
#endif
      sprintf(name, "bbfps[%d]", m);
      probe_setName(bbfps[m], name);
 
      DVBS2ll_addActualPayloadBitSizeProbe(dvbs2ll, m, bbfps[m]);
      for (q = 0; q < NB_QOS_LEVEL; q++) {
         // Création de la file
         files[m][q] = filePDU_create(schedks, (processPDU_t)schedACM_processPDU);

         // Le générateur de taille de paquet
         sizeGen[m][q] = randomGenerator_createUIntDiscreteProba(4,
							    packetSize,
							    packetSizeProba);
         taillePaquet[m][q] = randomGenerator_getExpectation(sizeGen[m][q]);

         // Le rythme de production des paquets dépend du débit et de
         // la taille des paquets
         lambda[m][q] = wghtQoS[q]
                      * rho[m]
                      * debitBinaire[m]
                      / (8.0*taillePaquet[m][q]);

         // Taille de la file d'attente
	 // WARNING tester toutes les tailles, et choisir une taille
	 // minimale plus justifiable
	 if (fileDuration > 0.0) {
            if (fileDuration* wghtQoS[q] * rho[m] * debitBinaire[m]/8.0 >= 4.0 * packetSize[3]) {
              filePDU_setMaxSize(files[m][q], fileDuration* wghtQoS[q] * rho[m] * debitBinaire[m]/8.0);
	    } else {// 2012-03-13 définition d'une taille de file minimale 
 	       filePDU_setMaxSize(files[m][q], 4.0 * packetSize[3]);
	       printf("*** Attention taille de file %d/%d trop courte (%f)\n",
	   	  m, q, fileDuration* wghtQoS[q] * rho[m] * debitBinaire[m]/8.0);
	    }
	 }
         
         //pertes[m][q] = probe_createExhaustive();
	 //pertes[m][q] = probe_createTimeSliceAverage(dureeSimulation/50.0);
	 pertes[m][q] = probe_createTimeSliceThroughput(dureeSimulation/50.0);
	 sprintf(name, "pertes[%d][%d]", m, q);
	 probe_setName(pertes[m][q], name);

	 filePDU_addDropSizeProbe(files[m][q], pertes[m][q]);

         // Le générateur de date de production
         dateGen[m][q] = dateGenerator_createExp(lambda[m][q]);

         // Création de la source 
         sourcePDU[m][q] = PDUSource_create(dateGen[m][q],
					    files[m][q],
					    (processPDU_t)filePDU_processPDU);
         PDUSource_setPDUSizeGenerator(sourcePDU[m][q],  sizeGen[m][q]); 

         // Ajout des sondes
#ifdef USE_EXHAUSTIVE_PROBES
	 sps[m][q] = probe_createExhaustive();
#else
	 sps[m][q] = probe_createMean();
#endif
	 sprintf(name, "sps[%d][%d]", m, q);
	 probe_setName( sps[m][q], name);
         PDUSource_addPDUGenerationSizeProbe(sourcePDU[m][q], sps[m][q]);

#ifdef USE_EXHAUSTIVE_PROBES
	 tms[m][q] = probe_createExhaustive();
#else
	 tms[m][q] = probe_createTimeSliceAverage(200.0/lambda[m][q]);
#endif
	 sprintf(name, "tms[%d][%d]", m, q);
	 probe_setName(tms[m][q], name);
         filePDU_addSejournProbe(files[m][q], tms[m][q]);
	 probe_addSampleProbe(tms[m][q], tmsGlobal);

         // On démarre la source
         PDUSource_start(sourcePDU[m][q]);

         // Les sondes pour les moyennes inter-simu
	 tmsMoyen[m][q] = probe_createExhaustive();
	 sprintf(name, "tmsMoyen[%d][%d]", m, q);
	 probe_setName(tmsMoyen[m][q], name);
         probe_setPersistent(tmsMoyen[m][q]);

	 nbPaquetsMoyen[m][q] = probe_createExhaustive();
	 sprintf(name, "nbPaquetsMoyen[%d][%d]", m, q);
	 probe_setName(nbPaquetsMoyen[m][q], name);
         probe_setPersistent(nbPaquetsMoyen[m][q]);

         // Choix de la stratégie de QoS
         schedACM_setFileQoSType(schedks, m, q, typeQoS[m][q], betaQoS[m][q], lambda[m][q]*8.0*taillePaquet[m][q]);

      }
      bbfpsMoyen[m] = probe_createExhaustive();
      sprintf(name, "bbfpsMoyen[%d]", m);
      probe_setName(bbfpsMoyen[m], name);
      probe_setPersistent(bbfpsMoyen[m]);
      schedACM_setInputQueues(schedks, m, files[m]);
   }

   // Les sondes de déclassement
   for (m = 0; m < NB_MODCOD; m++) {
      for (q = 0; q < NB_QOS_LEVEL; q++) {
         for (mc = 0; mc < NB_MODCOD; mc++) {
            pqinmc[m][q][mc] = probe_createMean();
	    schedACM_setPqFromMQinMC(schedks, m, q, mc, pqinmc[m][q][mc]);
         }
      }
   }

   // Les sondes permettant de visualiser le débit tel que l'estime l'ordo
   if (dumpThroughput) {
      for (m = 0; m < NB_MODCOD; m++) {
         for (q = 0; q < NB_QOS_LEVEL; q++) {
	   //            bwProbe[m][q] = probe_createExhaustive();
            bwProbe[m][q] = probe_createTimeSliceAverage(dureeSimulation/200.0);
            sprintf(name, "bwProbe[%d][%d]", m, q);
            probe_setName(bwProbe[m][q], name);
            schedACM_addThoughputProbe(schedks, m, q, bwProbe[m][q]);
         } 
      }

      // Les sondes permettant de visualiser le débit par une moyenne temporelle
      for (m = 0; m < NB_MODCOD; m++) {
         for (q = 0; q < NB_QOS_LEVEL; q++) {
            actualBwProbe[m][q] = probe_createTimeSliceThroughput(dureeSimulation/200.0);
            sprintf(name, "timeSliceBwProbe[%d][%d]", m, q);
            probe_setName(actualBwProbe[m][q], name);
            filePDU_addExtractSizeProbe(files[m][q], actualBwProbe[m][q]);

/*  POURQUOI LA SUITE NE FONCTIONNE PAS !?!?!?
	    emaBwProbe[m][q] = probe_EMACreate(0.9);
   	 //         sprintf(name, "[DB]emaBwProbe[%d][%d]", m, q);
            sprintf(name, "emaBwProbe[%d][%d]", m, q);
            probe_setName(emaBwProbe[m][q], name);
            filePDU_addExtractSizeProbe(files[m][q], emaBwProbe[m][q]);

            periodicEmaBwProbe[m][q] = probe_periodicCreate(dureeSimulation/10.0);
            sprintf(name, "periodicEmaBwProbe[%d][%d]", m, q);
            probe_setName(periodicEmaBwProbe[m][q], name);
	    probe_addThroughputProbe(emaBwProbe[m][q], periodicEmaBwProbe[m][q]);
*/
         }
      }
   }

   // WARNING : a faire  comparer le nbre de sous cas max avec taillemaxbbf/taillemin paquet

   /*
    * Affichage des caractéristiques des MODCODS
    */

   fprintf(fichierLog, "Version             :     %d", KS_VERSION);
#if defined(TEST_METEO)
   fprintf(fichierLog,"-meteo\n");
#elif defined(TEST_METEO_REF)
   fprintf(fichierLog,"-meteo-ref\n");
#else
   fprintf(fichierLog,"-vanilla\n");
#endif
   fprintf(fichierLog, "Nombre de MODCOD    :     %d\n", NB_MODCOD);
   fprintf(fichierLog, "Nombre de QoSlevels :     %d\n", NB_QOS_LEVEL);
   fprintf(fichierLog, "Charge globale      :   %5.2f\n", chargeEntree);
   fprintf(fichierLog, "Duree de simulation : %7.2f secondes\n", dureeSimulation);
   fprintf(fichierLog, "Declassement        : %s\n", declassement?"AVEC":"SANS");
   fprintf(fichierLog, "Duree d'epoque      : %f (%d frame min)\n", epochMinDuration, sequenceMaxLength);
   fprintf(fichierLog, "Fichier de sortie   : %s\n", filename);
   fprintf(fichierLog, "\n           Caracteristiques des MODCODs\n");
   fprintf(fichierLog, "---------+-----------+------------+-------------+\n");
   fprintf(fichierLog, "    m    |  tps      | bit length |  bitrate    |\n");
   fprintf(fichierLog, "---------+-----------+------------+-------------+\n");
   for (m = 0; m < NB_MODCOD; m++) {
      fprintf(fichierLog, "    %d    | %f  |  %5d     | %6.2f Mbps |\n",
	     m,
	     DVBS2ll_bbframeTransmissionTime(dvbs2ll, m),
	     DVBS2ll_bbframePayloadBitSize(dvbs2ll, m),
	     debitBinaire[m]/1000000.0);
   }

   fprintf(fichierLog, "---------+-----------+------------+-------------+\n");



   /*
    * Affichage des caractéristiques des sources
    */
   fprintf(fichierLog, "           Caracteristiques des sources\n");
   fprintf(fichierLog, "---------+-------------+-------------+----------------+---------------+\n");
   fprintf(fichierLog, "   m/q   |    lambda   |    taille   |      debit moy |  taille file  |\n");
   fprintf(fichierLog, "---------+-------------+-------------+----------------+---------------+\n");
   for (m = 0; m < NB_MODCOD; m++) {
      debit = 0.0;
      for (q = 0; q < NB_QOS_LEVEL; q++) {
         fprintf(fichierLog, "   %d/%d   |", m, q);
         fprintf(fichierLog, "  %8.2f   |", lambda[m][q]);
         fprintf(fichierLog, "   %7.1f   |", taillePaquet[m][q]);
         fprintf(fichierLog, " %7.3f Mbit/s |", lambda[m][q]*8.0*taillePaquet[m][q]/1000000.0);
         fprintf(fichierLog, "  %9lu B  |", filePDU_getMaxSize(files[m][q]));
         fprintf(fichierLog, "\n");
	 debit += lambda[m][q]*8.0*taillePaquet[m][q]/1000000.0;
      }
      fprintf(fichierLog, ".........+.............+.............+................+...............+\n");
      fprintf(fichierLog, "   %d/*   |", m);
      fprintf(fichierLog, "        na   |");
      fprintf(fichierLog, "        na   |");
      fprintf(fichierLog, " %7.3f Mbit/s |               |", debit);
      fprintf(fichierLog, "\n");
      fprintf(fichierLog, "---------+-------------+-------------+----------------+---------------+\n");
   }

#ifdef TEST_METEO
   scheduleMeteoEvent(0.4*dureeSimulation, dvbs2ll, 2, C25SIZE, M16APSK);
   scheduleMeteoEvent(0.6*dureeSimulation, dvbs2ll, 2, C34SIZE, M16APSK);
#endif

#ifdef TEST_INCIVISME   // On le fera sur le scenario 3 (1MC)
   scheduleChangementComportement(0.4*dureeSimulation, dateGen[3][0], lambda[3][0]*1.5);
   scheduleChangementComportement(0.6*dureeSimulation, dateGen[3][0], lambda[3][0]);
#endif


   for (ns = 1 ; ns <= NB_SIMULATIONS; ns++) {
 
      fprintf(fichierLog, "\n======= Simulation %d/%d =========\n", ns, NB_SIMULATIONS);
      dateDebut = time(NULL); 
      motSim_runUntil(dureeSimulation);
      // Pour faire comme avant (ligne ci dessous) il faut pouvoir
      //prévoir des événements qui ne soient pas purger par le motsim_Reset
      //motSim_runNSimu(dureeSimulation, 1);
      dateFin = time(NULL); 


      fprintf(fichierLog, "=================================\n");
      motSim_printStatus();

      //      printf("Nombre de remplissages %ld all/%ld free\n", nbRemplissageAlloc, nbRemplissageFree);
      //      printf("=================================\n");

      fprintf(fichierLog, " - Nombre de BBFRAME produites : %ld ", probe_nbSamples(sip));
      if (probe_nbSamples(sip)) fprintf(fichierLog, " (iap = %f)", probe_IAMean(sip));
      fprintf(fichierLog, "\n - Nombre de DUMMY produites : %ld\n", probe_nbSamples(df));
      fprintf(fichierLog, " - Durée moyenne d'une époque  : %lf\n", probe_mean(epochDurationProbe));
      fprintf(fichierLog, " - Nombre d'epoques            : %d (%d famines)\n", schedACM_getNbEpoch(schedks), schedACM_getNbEpochStarvation(schedks));
      fprintf(fichierLog, " - Nombre moyen de cas testes : %f\n", probe_mean(nbCas));
      fprintf(fichierLog, " - Duree en temps reel : %ld\n", dateFin - dateDebut);
      fprintf(fichierLog, "   m/q   |  packets  | BBFRAMEs |   size   |    ia    |    tms     | backlog  |  rempl.  |\n");
      fprintf(fichierLog, "---------+-----------+----------+----------+----------+------------+----------+----------+\n");
      for (m = 0; m < NB_MODCOD; m++) {
         nbPqTotal = 0;
         for (q = 0; q < NB_QOS_LEVEL; q++) {
            fprintf(fichierLog, "   %d/%d   |", m, q);
            fprintf(fichierLog, " %9ld |", probe_nbSamples(sps[m][q]));
	    nbPqTotal += probe_nbSamples(sps[m][q]);
            fprintf(fichierLog, "          |");
            if (probe_nbSamples(sps[m][q])) {
	       fprintf(fichierLog, "  %7.1f |", probe_mean(sps[m][q]));
	       fprintf(fichierLog, " %7.2e |", probe_IAMean(sps[m][q]));
            } else {
               fprintf(fichierLog, "    NA    |");
               fprintf(fichierLog, "    NA    |");
	    }
            if (probe_nbSamples(tms[m][q])) {
	       fprintf(fichierLog, "   %7.2e |", probe_mean(tms[m][q]));
            } else {
               fprintf(fichierLog, "    NA      |");
	    }
            fprintf(fichierLog, "  %6d  |", filePDU_length(files[m][q]));
            fprintf(fichierLog, "          |\n");


            fprintf(fichierLog, "         |");
            fprintf(fichierLog, "           |");
	    //            fprintf(fichierLog, "+/- %6.1f |", probe_demiIntervalleConfiance5pcCoupes(sps[m][q]));
            fprintf(fichierLog, "          |");
            fprintf(fichierLog, "          |");
            fprintf(fichierLog, "          |");
            if (probe_nbSamples(tms[m][q])) {
               fprintf(fichierLog, "+/- %5.1e |", probe_demiIntervalleConfiance5pc(tms[m][q]));
            } else {
               fprintf(fichierLog, "    NA      |");
	    }

            fprintf(fichierLog, "          |");
            fprintf(fichierLog, "          |\n");
	 }
         fprintf(fichierLog, ".........+...........+..........+..........+..........+............+..........+..........+\n");

         fprintf(fichierLog, "   %d/*   |", m);
         fprintf(fichierLog, " %9d |", nbPqTotal);
         fprintf(fichierLog, " %7ld  |", probe_nbSamples(bbfps[m]));
         fprintf(fichierLog, "          |");
         fprintf(fichierLog, "          |");
         fprintf(fichierLog, "            |");
         fprintf(fichierLog, "          |");
         if (probe_nbSamples(bbfps[m])) {
            fprintf(fichierLog, "  %5.2f %% |\n", 100.0*probe_mean(bbfps[m])/(double)DVBS2ll_bbframePayloadBitSize(dvbs2ll, m));
         } else {
            fprintf(fichierLog, "    na    |\n");
         }
         fprintf(fichierLog, "---------+-----------+----------+----------+----------+------------+----------+----------+\n");
      }
      fprintf(fichierLog, "   */*   |");
      fprintf(fichierLog, "           |");
      fprintf(fichierLog, "          |");
      fprintf(fichierLog, "          |");
      fprintf(fichierLog, "          |");
      fprintf(fichierLog, "   %7.2e |", probe_mean(tmsGlobal));
      fprintf(fichierLog, "          |");
      fprintf(fichierLog, "          |\n");
      fprintf(fichierLog, "         |");
      fprintf(fichierLog, "           |");
      fprintf(fichierLog, "          |");
      fprintf(fichierLog, "          |");
      fprintf(fichierLog, "          |");
      fprintf(fichierLog, "+/- %5.1e |", probe_demiIntervalleConfiance5pc(tmsGlobal));
      fprintf(fichierLog, "          |");
      fprintf(fichierLog, "          |\n");
      fprintf(fichierLog, "---------+-----------+----------+----------+----------+------------+----------+----------+\n");


      /* Répartitions des flots dans les MODCODS */
      fprintf(fichierLog, "\n\nRépartitions des flots dans les MODCODS:\n");
      fprintf(fichierLog, "+---------+------------------+------------------+------------------+------------------+----------+---------+\n");
      fprintf(fichierLog, "|  File   |       MC 1       |       MC 2       |       MC 3       |       MC 4       |   Total  |   Lost  |\n");
      fprintf(fichierLog, "+---------+------------------+------------------+------------------+------------------+----------+---------+\n");
      for (m = 0; m < NB_MODCOD; m++) {
         for (q = 0; q < NB_QOS_LEVEL; q++) {
            volumeTotal = 0.0;
	    nbPqTotal = 0;
            for (mc = 0; mc < NB_MODCOD; mc++) {
	       nbPqTotal += probe_nbSamples(pqinmc[m][q][mc]);
               volumeTotal += probe_nbSamples(pqinmc[m][q][mc])*probe_mean(pqinmc[m][q][mc]);
	    }

            fprintf(fichierLog, "|  M%d/Q%d  |", m, q);

            // Nombre de paquets
            for (mc = 0; mc < NB_MODCOD; mc++) {
	      fprintf(fichierLog, " %6lu (%6.2f%%) |", probe_nbSamples(pqinmc[m][q][mc]),
		      100.0*(float)probe_nbSamples(pqinmc[m][q][mc])/(float)nbPqTotal);
	    }
            fprintf(fichierLog, "   %6d |", nbPqTotal);
            fprintf(fichierLog, "  %6ld |\n", probe_nbSamples(pertes[m][q]));

            // Taux de déclassement
            fprintf(fichierLog, "|  PqDecl |");
            for (mc = 0; mc < NB_MODCOD; mc++) {
               fprintf(fichierLog, "                  |");
	    }
	    nbPqTotal = 0;
	    nbPq = 0;
            for (mc = 0; mc < NB_MODCOD; mc++) {
	       nbPqTotal += probe_nbSamples(pqinmc[m][q][mc]);
	       if (mc != m) {
	          nbPq += probe_nbSamples(pqinmc[m][q][mc]);
	       }
	    }
            fprintf(fichierLog, "  %6.2f%% |", 100.0*nbPq/nbPqTotal);
            fprintf(fichierLog, " %6.2f%% |\n", 100.0*probe_nbSamples(pertes[m][q])/(nbPqTotal+probe_nbSamples(pertes[m][q])));

	 }
         fprintf(fichierLog, "+---------+------------------+------------------+------------------+------------------+----------+---------+\n");
      }
      // Nombre de paquets émis sur chaque MODCOD
      fprintf(fichierLog, "| nbPq    |");
      for (mc = 0; mc < NB_MODCOD; mc++) {
         nbPqTotal = 0;
         for (m = 0; m < NB_MODCOD; m++) {
            for (q = 0; q < NB_QOS_LEVEL; q++) {
	       nbPqTotal += probe_nbSamples(pqinmc[m][q][mc]);
            }
	 }
         fprintf(fichierLog, " %6d           |", nbPqTotal);
      }
      nbPqTotal = 0;
      for (mc = 0; mc < NB_MODCOD; mc++) {
         for (m = 0; m < NB_MODCOD; m++) {
            for (q = 0; q < NB_QOS_LEVEL; q++) {
	       nbPqTotal += probe_nbSamples(pqinmc[m][q][mc]);
            }
	 }
      }
      fprintf(fichierLog, "   %6d |\n", nbPqTotal);

      // Taux de déclassement
      fprintf(fichierLog, "| PqDecl  |");
      for (mc = 0; mc < NB_MODCOD; mc++) {
         nbPqTotal = 0;
         nbPq = 0;
         for (m = 0; m < NB_MODCOD; m++) {
            for (q = 0; q < NB_QOS_LEVEL; q++) {
	       nbPqTotal += probe_nbSamples(pqinmc[m][q][mc]);
	       if (mc != m) {
	          nbPq += probe_nbSamples(pqinmc[m][q][mc]);
	       }
            }
	 }
         fprintf(fichierLog, " %6d (%6.2f%%) |", nbPq, 100.0*nbPq/nbPqTotal);
      }
      fprintf(fichierLog, "          |\n");


      // Nombre de BBFAMEs émises sur chaque MODCOD
      fprintf(fichierLog, "| nbBB    |");
      nbPqTotal = 0;
      for (mc = 0; mc < NB_MODCOD; mc++) { 
         nbPqTotal += probe_nbSamples(bbfps[mc]);
         fprintf(fichierLog, " %6lu           |", probe_nbSamples(bbfps[mc]));
      }
      fprintf(fichierLog, "  %6d  |\n", nbPqTotal);

      // Taux de remplissage des BBFRAMEs
      fprintf(fichierLog, "| TxRp    |");
      for (m = 0; m < NB_MODCOD; m++) {
         fprintf(fichierLog, " %6.2f           |", probe_mean(bbfps[m])/(double)DVBS2ll_bbframePayloadBitSize(dvbs2ll, m));
      }
      fprintf(fichierLog, "          |\n");
      fprintf(fichierLog, "+---------+------------------+------------------+------------------+------------------+----------+\n");

      /* Création des fichiers de pertes */
      if (dumpDropProbe) {
         printf("On dump les pertes\n");
         for (m = 0; m < NB_MODCOD; m++) {
            for (q = 0; q < NB_QOS_LEVEL; q++) {
               sprintf(dataFilename, "%s-drop-%d-%d.data", filename, m, q);
	       if ((fdDebit = open(dataFilename,O_CREAT|O_WRONLY, 0644)) == -1 ) {
	          perror("open");
 	          exit(1);
	       }
               probe_dumpFd(pertes[m][q], fdDebit, dumpGnuplotFormat);
               close(fdDebit);
	    }
         }
      }


      /* Création des fichiers décrivant le débit estimé par l'ordonnanceur */
      if (dumpThroughput) {
         printf("On dump les debits observes par le scheduler\n");
         for (m = 0; m < NB_MODCOD; m++) {
            for (q = 0; q < NB_QOS_LEVEL; q++) {
               sprintf(dataFilename, "%s-debit-ks-%d-%d.data", filename, m, q);
	       if ((fdDebit = open(dataFilename,O_CREAT|O_WRONLY, 0644)) == -1 ) {
	          perror("open");
 	          exit(1);
	       }
               probe_dumpFd(bwProbe[m][q], fdDebit, dumpGnuplotFormat);
               close(fdDebit);
	    }
         }
 
         /* Création des fichiers décrivant le débit mesuré par moyenne temporelle */
         printf("On dump les debits observes par moyenne temporelle\n");
         for (m = 0; m < NB_MODCOD; m++) {
            for (q = 0; q < NB_QOS_LEVEL; q++) {
               sprintf(dataFilename, "%s-debit-mt-%d-%d.data", filename, m, q);
	       if ((fdDebit = open(dataFilename,O_CREAT|O_WRONLY, 0644)) == -1 ) {
	          perror("open");
 	          exit(1);
	       }
               probe_dumpFd(actualBwProbe[m][q], fdDebit, dumpGnuplotFormat);
               close(fdDebit);
	    }
         }

         /* Création des fichiers décrivant le débit mesuré par moyenne mobile */
/*       printf("On dump les debits observes par EMA\n");
         for (m = 0; m < NB_MODCOD; m++) {
            for (q = 0; q < NB_QOS_LEVEL; q++) {
               sprintf(dataFilename, "%s-debit-ma-%d-%d.data", filename, m, q);
	       if ((fdDebit = open(dataFilename,O_CREAT|O_WRONLY, 0644)) == -1 ) {
	          perror("open");
 	          exit(1);
	       }
               probe_dumpFd(periodicEmaBwProbe[m][q], fdDebit, dumpGnuplotFormat);
               close(fdDebit);
            }
         }
*/
      }
      /* Les résultats pour un gnuplot */
      max = 0.0;
      for (m = 0; m < NB_MODCOD; m++) {
         for (q = 0; q < NB_QOS_LEVEL; q++) {
            if (probe_nbSamples(tms[m][q])) { 
	    max = (max > probe_mean(tms[m][q]))? max : probe_mean(tms[m][q]);
            fprintf(fichierData, "%d M%d/Q%d %f %f\n", 4*m+q+1, m+1, q+1, probe_mean(tms[m][q]), probe_demiIntervalleConfiance5pc(tms[m][q]));
	    } else {
               fprintf(fichierData, "%d M%d/Q%d %f %f\n", 4*m+q+1, m+1, q+1, 0.0, 0.0);
            }
         }
      }

      fprintf(fichierGnuplot, "set terminal postscript color dashed\n");
      fprintf(fichierGnuplot, "set output '%s.eps'\n", filename);
      fprintf(fichierGnuplot, "set xrange [0:16]\n");
      fprintf(fichierGnuplot, "set yrange [0:%f]\n", 1.05*max);
      fprintf(fichierGnuplot, "set boxwidth 0.5\n");
      fprintf(fichierGnuplot, "plot '%s.data' using 1:3:xtic(2) with boxes title 'Response Time', '%s.data' using 1:3:4 with yerrorbars title 'Confidence Interval' \n", filename, filename);
      fprintf(fichierGnuplot, "!ps2pdf %s.eps %s.pdf\n", filename, filename);
      fprintf(fichierGnuplot, "!rm -f %s.eps\n", filename);

#ifdef OUTPUT_PNG
      fprintf(fichierGnuplot, "set terminal png\n");
      fprintf(fichierGnuplot, "set output '%s.png'\n", filename);
      fprintf(fichierGnuplot, "set xrange [0:16]\n");
      fprintf(fichierGnuplot, "set yrange [0:%f]\n", 1.05*max);
      fprintf(fichierGnuplot, "set boxwidth 0.5\n");
      fprintf(fichierGnuplot, "set style fill solid 1.00 border 0\n");
      fprintf(fichierGnuplot, "plot '%s.data' using 1:3:xtic(2) with boxes title 'Response Time' linecolor rgb '#808080', '%s.data' using 1:3:4 with yerrorbars title 'Confidence Interval' linecolor rgb 'black'\n", filename, filename);
#endif 
      // On insère les résultat de la simulation dans les moyennes
      for (m = 0; m < NB_MODCOD; m++) {
         for (q = 0; q < NB_QOS_LEVEL; q++) {
            if (probe_nbSamples(tms[m][q])) { 
               probe_sample(tmsMoyen[m][q], probe_mean(tms[m][q]));
	    };
            if (probe_nbSamples(sps[m][q])) { 
               probe_sample(nbPaquetsMoyen[m][q], probe_nbSamples(sps[m][q]));
	    }
         }
         if (probe_nbSamples(bbfps[m])) { 
            probe_sample(bbfpsMoyen[m], probe_mean(bbfps[m]));
	 }
      }

      // Et on ré-initalise pour une nouvelle tournée !
      motSim_reset();
   }

   if (probe_nbSamples(tmsMoyen[0][0]) > 1) {
      printf("\n\nFin de campagne\n");
      printf("Sur %ld simulations :\n", probe_nbSamples(tmsMoyen[0][0]));


      printf(" - Nombre de BBFRAME produites : --");

      printf("\n - Nombre de DUMMY produites : --\n");

      printf("   m/q   |  packets  | BBFRAMEs |   size   |    ia    |      tms    | backlog  |  rempl.  |\n");
      printf("---------+-----------+----------+----------+----------+-------------+----------+----------+\n");
      for (m = 0; m < NB_MODCOD; m++) {
         for (q = 0; q < NB_QOS_LEVEL; q++) {
            printf("   %d/%d   |", m, q);
            printf("  %8.1f |", probe_mean(nbPaquetsMoyen[m][q]));
            printf("          |");
            printf("          |");
            printf("          |");
            printf(" %7.2e    |", probe_mean(tmsMoyen[m][q]));

            printf("          |");
            printf("          |\n");

            printf("         |");
            printf("           |");
            printf("          |");
            printf("          |");
            printf("          |");
            printf("+/- %7.2e |", probe_demiIntervalleConfiance5pc(tmsMoyen[m][q]));

            printf("          |");
            printf("          |\n");
	 }
         printf(".........+...........+..........+..........+..........+.............+..........+..........+\n");

         printf("   %d/*   |", m);
         printf("  A FAIRE  |");
         printf("          |");
         printf("          |");
         printf("          |");
         printf("             |");
         printf("          |");
         if (probe_nbSamples(bbfpsMoyen[m])) {
            printf("  %5.2f %% |\n", 100.0*probe_mean(bbfpsMoyen[m])/(double)DVBS2ll_bbframePayloadBitSize(dvbs2ll, m));
	 }else {
            printf("    na    |\n");
	 }
         printf("---------+-----------+----------+----------+----------+-------------+----------+----------+\n");
      }
   }

   fclose(fichierLog);
   fclose(fichierGnuplot);
   fclose(fichierData);

   return 1;
}
