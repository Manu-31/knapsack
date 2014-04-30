#!/bin/bash
#--------------------------------------------------------------------
# Script permettant de lancer une campage de simulation avec mon
# modèle QNAP
#
#   usage : LancerCampagne.sh fichiercamp
#--------------------------------------------------------------------
#
#   2011-04-24 : Possibilite d'utiliser NDES à la place de QNAP
#   2014-02-10 : Gestion des traitements par lot
#   2014-04-28 : Début d'utilisation de fichiers de configuration
#--------------------------------------------------------------------
CONFIGDIR=$HOME/PROGRAMMATION/C/GSE-SCHED-CAMPAIGNS/ConfigDir/

#--------------------------------------------------------------------
#   Chargement des paramètres par défaut
#--------------------------------------------------------------------

echo Reading default parameters
. $CONFIGDIR/campagne.default

#--------------------------------------------------------------------
#   Chargement des paramètres de la campagne
#--------------------------------------------------------------------
if [ -n "$1" -a -f "$1" ] ; then 
   echo Reading specific parameters
   . $1
   CAMPAIGN_NAME=`basename $1`
else 
   echo No config file 
   CAMPAIGN_NAME=default
fi

#--------------------------------------------------------------------
#   Finalisation des paramètres
#--------------------------------------------------------------------
echo Building simulator parameters
. $CONFIGDIR/ConfigureCampaign

echo Running simulations

touch  ${LOG}
echo "---------------------------------------------------------------------" >> ${LOG}
echo ">>>> " `hostname` -- `date` : debut de $CAMPAIGN_NAME >> ${LOG}
echo "      (dans " `pwd` ")">> ${LOG}

NB_SIMU=0

for algo in $ALGO
do
for charge in $CHARGES
do
   echo Charge = $charge
   for scenario in $SCENARIOS
   do
      echo "   " Scenario : $scenario
      for n in $(seq 0 $((${#QoSSet[@]} / 3 - 1)))
      do
         m=$((3 * $n))
         typeqos1=${QoSSet[$m]}
         typeqos2=${QoSSet[$(($m + 1))]}
         typeqos3=${QoSSet[$(($m + 2))]}
         for p in $(seq 0 $((${#WghtQoS[@]} / 3 - 1)))
         do
            w=$((3 * $p))
            wghtqos1=${WghtQoS[$w]}
            wghtqos2=${WghtQoS[$(($w + 1))]}
            wghtqos3=${WghtQoS[$(($w + 2))]}

            for ks in $(seq 0 $((${#KSet[@]} / 3 - 1)))
            do
               k=$((3 * $ks))
               k1=${KSet[$k]}
               k2=${KSet[$(($k + 1))]}
               k3=${KSet[$(($k + 2))]}

               for declassement in $DECLASSEMENT
               do
                  echo "         " Declassement : $declassement
                  filename=${RESDIR}/sc${scenario}-ch${charge}-alg${algo}-qos${typeqos1}${typeqos2}${typeqos3}-w${wghtqos1},${wghtqos2},${wghtqos3}-k${k1},${k2},${k3}-dc${declassement}
                  echo "**** Fichier de sortie : " ${filename}
		  MC1=${MODCOD[0]}
		  MC2=${MODCOD[1]}
		  MC3=${MODCOD[2]}
		  MC4=${MODCOD[3]}
                  if [ $SIMULATEUR = QNAP ]
                  then
                     \rm -f ${filename}.log
                     tmp_filename=/tmp/qnap-output-$$
                     cat <<EOF |$QNAP 
$SOURCE
N
Y
KB
FI
$tmp_filename
N
N
$DUREE
$charge
$scenario
$algo
$typeqos1
$typeqos2
$typeqos3
$declassement
$wghtqos1
$wghtqos2
$wghtqos3
$k1
$k2
$k3
EOF
                  mv $tmp_filename ${filename}.log
                  else
                     cat <<EOF_NDES > ${filename}.param
$DUREE
$MC1
$MC2
$MC3
$MC4
$charge
$scenario
$algo
$EPOCH_DURATION
$typeqos1
$typeqos2
$typeqos3
$declassement
$wghtqos1
$wghtqos2
$wghtqos3
$k1
$k2
$k3
$filename
$LOG_BW
$LOG_DROP
$FILE_DURATION
EOF_NDES
                     # La découpe ci dessous en MC/QO/NbSrc n'est pas vraiment nécessaire !           
                     (for n in $(seq 0 $((${#NOMBRE_SOURCES[@]} / 3 - 1)))
                     do
                        m=$((3 * $n))
                        MC=${NOMBRE_SOURCES[$m]}
                        QO=${NOMBRE_SOURCES[$(($m + 1))]}
                        nbSrc=${NOMBRE_SOURCES[$(($m + 2))]}
                        echo $MC
                        echo $QO 
                        echo $nbSrc
                     done
                     echo -1) >> ${filename}.param
	             cat ${filename}.param  |$NDES
                  fi
                  NB_SIMU=`expr ${NB_SIMU} + 1`
               done
            done
         done
      done
   done
done
done


echo "<<<< "  `hostname` --  `date` : FIN de $CAMPAIGN_NAME "(" ${NB_SIMU} " simu)" >> ${LOG}
echo "---------------------------------------------------------------------" >> ${LOG}

