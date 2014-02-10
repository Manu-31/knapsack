Simulateur "knapsack"
=====================

Cet outil est un simulateur permettant d'évaluer des algorithmes d'ordonnancement
de paquets sur un lien de type DVB-S2. Il s'agit d'un programme interactif simple
mais il ne doit a priori pas être utilisé de cette façon. Un script de lancement
nommé LancerCampagne.sh est fourni permettant de l'utiliser de façon plus simple.

De la même façon, l'essentiel du code n'est pas dans le programme knapsack.c lui
même, mais dans une librairie, ndes. C'est donc dans cette librairie qu'on trouvera
la quasi totalité des détails d'implantation.

Installation
------------

Lorsque la librairie de simulation ndes a été compilée et installée, le répertoire
dans lequel elle a été installée doit être précisé dans le Makefile. Ce dernier
contient par défaut :

NDES_ROOT_DIR=../ndes

Le simulateur peut alors être compilé par 

% make

et il sera installé (par défaut dans $HOME/bin) par

% make install

Avant cela, on vérifiera le contenu du fichier Makefile et on l'adaptera à ses
besoins.

Utilisation de base
-------------------

Voici un exemple simple d'utilisation interactive de knapsack :

% ./knapsack
Duree de la simulation (en secondes) = 100
Charge en entree       = 0.9
Scenario (0:eql,1:clr,2:inv,3:1mc) = 1
Algo (0=KS, 1=propUtil, 2=util, 3=ks exhau) = 1
File 0 : quelle QoS ? (1=log/2=lin/3=exp/4=exn/5=PQ/6=BT/7=BB) : 1
File 1 : quelle QoS ? (1=log/2=lin/3=exp/4=exn/5=PQ/6=BT/7=BB) : 1
File 2 : quelle QoS ? (1=log/2=lin/3=exp/4=exn/5=PQ/6=BT/7=BB) : 1
Declassement (TRUE/FALSE) ? = FALSE
File 0 : quel poids ? : 11
File 1 : quel poids ? : 1
File 2 : quel poids ? : 1
File 0 : quel QoS param ? : 1
File 1 : quel QoS param ? : 1
File 2 : quel QoS param ? : 1
Nom du fichier : exemple
Fichiers de debit (TRUE/FALSE) ? = TRUE
Fichiers de pertes (TRUE/FALSE) ? = TRUE
Capacite de chaque file (en secondes) =  1
Ouverture des fichiers de log ...
Creation du simulateur
[MOTSI] Date = 100.000000
[MOTSI] Events : 756107 created (63 m + 756044 r)/756045 freed
[MOTSI] Simulated events : 756107 in, 756045 out, 62 pr.
[MOTSI] PDU (48 bytes): 1431782 created (16721 m + 1415061 r)/1415731 released
[MOTSI] Total malloc'ed memory : 56946240 bytes
[MOTSI] Realtime duration : 1 sec
On dump les debits observes par le scheduler
On dump les debits observes par moyenne temporelle
%

Comme dit précédemment, ce n'est pas comme cela qu'on utilisera le simulateur. On
préfèrera l'invoquer au travers de la commande LancerCampagne.sh

Avant de décrire cette commande et son paramétrage, observons les fichiers générés
par une telle simulation.

Fichiers générés
----------------

Si le nom fourni au simulateur est "exemple" alors tous les fichiers générés auront
un nom commençant par "exemple". Ils sont tous générés dans le répertoire courant :

% ls exemple*
exemple.data               exemple-debit-ks-1-2.data  exemple-debit-ks-3-2.data  exemple-debit-mt-1-2.data  exemple-debit-mt-3-2.data  exemple-drop-1-2.data  exemple-drop-3-2.data
exemple-debit-ks-0-0.data  exemple-debit-ks-2-0.data  exemple-debit-mt-0-0.data  exemple-debit-mt-2-0.data  exemple-drop-0-0.data      exemple-drop-2-0.data  exemple.gnu
exemple-debit-ks-0-1.data  exemple-debit-ks-2-1.data  exemple-debit-mt-0-1.data  exemple-debit-mt-2-1.data  exemple-drop-0-1.data      exemple-drop-2-1.data  exemple.log
exemple-debit-ks-0-2.data  exemple-debit-ks-2-2.data  exemple-debit-mt-0-2.data  exemple-debit-mt-2-2.data  exemple-drop-0-2.data      exemple-drop-2-2.data
exemple-debit-ks-1-0.data  exemple-debit-ks-3-0.data  exemple-debit-mt-1-0.data  exemple-debit-mt-3-0.data  exemple-drop-1-0.data      exemple-drop-3-0.data
exemple-debit-ks-1-1.data  exemple-debit-ks-3-1.data  exemple-debit-mt-1-1.data  exemple-debit-mt-3-1.data  exemple-drop-1-1.data      exemple-drop-3-1.data


Le fichier exemple.log résume la simulation d'une façon lisible par un utilisateur 
humain. Il est en revanche délicat à exploité automatiquement. On préfèrera les autres
fichiers.

Les fichiers *.data sont des fichiers textes contenant des résultats de simulation
exploitable par des outils tels que gnuplot. Le fichier exemple.gnu permet de générer
un fichier graphique donnant les temps moyen de service par file.

Les fichiers *-debit-* donne une mesure du débit. La version ks représente le débit
évalué par l'ordonnanceur, et la version mt est obtenue par une moyenne mobile qui
permet de vérifier la pertinence de ce qu'évalue l'ordonnanceur. Les nombres suivants
correspondent au numéro de MODCOD puis de file.

Les fichiers *-drop-* mesure le débit des pertes (nul sil les files sont infinies !).

Script LancerCampagne.sh
------------------------

C'est donc par ce script que sera préférentiellement lancé le simulateur. Pour cela,
on créera un fichier de configuration campagne.conf qu'on passera en paramètre au
script :

% LancerCampagne.sh campagne.conf

Le script lancera alors à la suite autant de simulation que voulu. Chaque simulation
donnera un ensemble de fichiers différents.

Le script est également configuré par un fichier par défaut nommé defaut.campagne
et qui sera placé dans le répertoire de configuration.

Configuration d'une campagne
----------------------------

La configuration d'une campagne passe donc par la création d'un fichier. Celui-ci
pourra être une copie du fichier  defaut.campagne dans laquelle on ne gardera que
les champs que l'on souhaite modifier.
