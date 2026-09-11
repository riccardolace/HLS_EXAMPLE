#!/usr/bin/env python3
# =============================================================================
#  create_workspace.py  --  Crea il workspace Vitis e il component HLS
# =============================================================================
#
#  COME SI LANCIA
#
#        vitis -s scripts/create_workspace.py
#
#  (oppure  make workspace )
#
#  Dopo di che il workspace si apre con la GUI:
#
#        vitis -w workspace
#
#  --------------------------------------------------------------------------
#  PERCHE' UNO SCRIPT E NON IL WIZARD GRAFICO
#  --------------------------------------------------------------------------
#  Non e' per evitare la GUI: e' esattamente la stessa cosa. "vitis -s" usa
#  la IDENTICA API Python che la GUI usa quando clicchi. Farlo da script serve a
#  tre cose:
#
#    1. il progetto e' riproducibile: se cancelli tutto, un comando lo rifa'
#       identico, e le scelte fatte sono scritte nero su bianco invece che
#       sepolte in una tendina di un wizard;
#    2. finisce in git e si puo' leggere in un diff;
#    3. evita il tranello descritto sotto, che il wizard grafico ti fa cadere
#       addosso di default.
#
#  Al contrario: quando fai qualcosa nella GUI, il tool te la trascrive da solo
#  in  workspace/_ide/workspace_journal.py . Aprilo ogni tanto: e' il modo piu'
#  rapido per imparare i comandi partendo dai click.
#  --------------------------------------------------------------------------
#
# =============================================================================
import os
import sys

# La cartella radice del progetto (il genitore di scripts/)
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORKSPACE = os.path.join(ROOT, "workspace")

COMPONENT = "axis_scaler"

# La parte target: VCK190 (Versal AI Core VC1902).
#
#   xc        commercial
#   vc        Versal AI Core
#   1902      modello
#   vsva2197  package vsva, 2197 pin
#   2MP       speed grade 2, classe di alimentazione MP (medium power)
#   e         temperatura extended, 0..100 C
#   S         binning di corrente di dispersione: S standard (l'altra e' L)
#
# E' la stessa parte della piattaforma xilinx_vck190_base che useremo piu'
# avanti per l'hardware emulation, quindi restiamo coerenti fin dall'inizio.
PART = "xcvc1902-vsva2197-2MP-e-S"


# =============================================================================
#  IL FILE DI CONFIGURAZIONE DEL COMPONENT
# =============================================================================
#
#  hls_config.cfg e' IL file che descrive il progetto HLS. Lo leggono e lo
#  scrivono tutti: la GUI, v++, vitis-run. Se lo modifichi a mano e riapri la
#  GUI, la GUI vede la modifica. Sono la stessa cosa.
#
#  Le chiavi qui dentro sono le stesse opzioni dei comandi Tcl "config_*" che
#  trovi nei tutorial vecchi, solo scritte come testo invece che come comandi:
#
#        syn.rtl.reset=control     ==   config_rtl -reset control
#        cosim.random_stall=1      ==   config_cosim -random_stall
#        package.ip.vendor=X       ==   config_export -vendor X
#
# =============================================================================
HLS_CONFIG = """\
# =============================================================================
#  Configurazione del component HLS "axis_scaler"
#  Generato da scripts/create_workspace.py -- modificabile a mano o dalla GUI.
# =============================================================================

# --- La parte target ---------------------------------------------------------
#  Nota: qui serve la PART, non la board. HLS non usa mai i board file: quelli
#  servono solo a Vivado, quando deve sapere piedinatura, DDR e clock di una
#  scheda fisica.
part=xcvc1902-vsva2197-2MP-e-S

[hls]

# --- Che tipo di IP vogliamo produrre ----------------------------------------
#
#  ATTENZIONE, QUESTO E' IL TRANELLO PIU' COMUNE DELLA 2025.2.
#
#  Il wizard "New HLS Component" della GUI di default scrive:
#
#        flow_target=vitis
#        package.output.format=xo
#
#  cioe' produce un kernel .xo per il flusso v++ / extensible platform, NON una
#  IP per il catalogo di Vivado. Poi cerchi lo .zip da importare in Vivado e non
#  lo trovi, e non capisci perche'.
#
#  Noi vogliamo una IP di catalogo, quindi:
flow_target=vivado
package.output.format=ip_catalog

# --- I sorgenti --------------------------------------------------------------
#
#  I percorsi sono relativi a QUESTO file (workspace/axis_scaler/).
#  I sorgenti stanno fuori dal workspace, in src/ e tb/: cosi' restano
#  indipendenti dal tool e puliti da versionare.
#
#  syn.file = codice che viene SINTETIZZATO (diventa hardware)
#  tb.file  = codice che gira solo sul PC per verificare (non diventa hardware)
#
#  La distinzione e' netta e vale la pena interiorizzarla subito: e' la
#  differenza fra la tua entity e il tuo testbench in VHDL.
syn.file=../../src/axis_scaler.cpp
syn.top=axis_scaler
tb.file=../../tb/axis_scaler_tb.cpp

# Il testbench sta in tb/ ma include un header che sta in src/, quindi va detto
# al compilatore dove cercarlo. E' il classico -I di gcc.
#
#   tb.cflags       vale per tutti i file di testbench
#   tb.file_cflags  permetterebbe di darne di diversi file per file
#
# (esistono i gemelli syn.cflags / syn.file_cflags per i sorgenti sintetizzati)
tb.cflags=-I../../src

# --- Periodo di clock in nanosecondi -----------------------------------------
#
#  4.0 ns = 250 MHz. E' il vincolo che HLS deve rispettare quando decide quante
#  operazioni infilare in un colpo di clock: e' l'ingresso principale
#  dell'algoritmo di scheduling, non un commento.
clock=4.0

# --- Stile del reset ---------------------------------------------------------
#
#  reset_level=low   -> reset attivo basso, cioe' ap_rst_n. E' la convenzione
#                       AXI, e infatti quando c'e' un'interfaccia AXI HLS lo
#                       fa gia' da solo. Lo scriviamo esplicito per chiarezza.
#
#  reset=control     -> vengono azzerati dal reset SOLO i registri di controllo
#                       (la macchina a stati), non tutti i registri di dato.
#                       E' il default ed e' la scelta giusta per l'area e il
#                       timing, ma ha una conseguenza importante che vedremo
#                       al gradino 1.7, quando introdurremo le variabili static.
syn.rtl.reset=control
syn.rtl.reset_level=low
"""


def main():
    # Importiamo vitis qui dentro: cosi' se qualcuno lancia questo file con
    # python3 normale invece che con "vitis -s", riceve un messaggio chiaro
    # invece di uno stack trace.
    try:
        import vitis
    except ImportError:
        print("ERRORE: questo script va lanciato con l'interprete di Vitis:")
        print("        vitis -s scripts/create_workspace.py")
        return 1

    comp_dir = os.path.join(WORKSPACE, COMPONENT)
    cfg_path = os.path.join(comp_dir, "hls_config.cfg")

    # Se il component esiste gia' non lo ricreiamo: riscriviamo solo la
    # configurazione. Cosi' questo script si puo' rilanciare quante volte si
    # vuole senza distruggere il lavoro fatto nella GUI.
    if os.path.isfile(os.path.join(comp_dir, "vitis-comp.json")):
        print("Component gia' presente: aggiorno solo hls_config.cfg")
        with open(cfg_path, "w") as f:
            f.write(HLS_CONFIG)
        print("  ->", cfg_path)
        return 0

    print("Creo il workspace:", WORKSPACE)
    client = vitis.create_client()
    client.set_workspace(WORKSPACE)

    print("Creo il component HLS:", COMPONENT)
    client.create_hls_component(name=COMPONENT)

    # Il wizard ha scritto un hls_config.cfg di default (con flow_target=vitis:
    # vedi la nota nel testo della configurazione). Lo sostituiamo interamente
    # con il nostro, che e' commentato e fa quello che vogliamo noi.
    print("Scrivo la configurazione:", cfg_path)
    with open(cfg_path, "w") as f:
        f.write(HLS_CONFIG)

    vitis.dispose()

    print("")
    print("Fatto. Ora puoi aprire la GUI con:")
    print("    vitis -w", WORKSPACE)
    return 0


if __name__ == "__main__":
    sys.exit(main())
