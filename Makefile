# =============================================================================
#  Makefile  --  l'equivalente da riga di comando di ogni azione della GUI
# =============================================================================
#
#  Questo Makefile NON e' un'alternativa alla GUI: e' la stessa cosa scritta in
#  un altro modo. Agisce sullo stesso workspace, sullo stesso hls_config.cfg e
#  scrive i risultati nella stessa cartella in cui li scrive la GUI. Puoi
#  lanciare la sintesi da qui e poi aprire il report nella GUI, o viceversa.
#
#  A cosa serve allora? A tre cose:
#    - rifare tutto da zero con un comando quando qualcosa si incarta;
#    - vedere in chiaro quale comando corrisponde a quale bottone;
#    - avere log completi da leggere quando un errore va investigato.
#
#  Digita  make  senza argomenti per l'elenco dei bersagli.
#
# =============================================================================

# Serve bash per poter usare "source".
SHELL := /bin/bash

# -----------------------------------------------------------------------------
#  Ambiente Xilinx
# -----------------------------------------------------------------------------
#  settings64.sh e' lo script che mette i tool nel PATH e imposta le variabili
#  d'ambiente. Va eseguito in ogni shell prima di usare v++, vitis, vivado.
XILINX_VERSION := 2025.2
XILINX_ROOT    := /tools/Xilinx/$(XILINX_VERSION)
SETUP_VITIS    := source $(XILINX_ROOT)/Vitis/settings64.sh
SETUP_VIVADO   := source $(XILINX_ROOT)/Vivado/settings64.sh

# -----------------------------------------------------------------------------
#  Percorsi del progetto
# -----------------------------------------------------------------------------
COMPONENT := axis_scaler
WORKSPACE := workspace
COMP_DIR  := $(WORKSPACE)/$(COMPONENT)
CFG       := hls_config.cfg

#  La cartella di lavoro in cui finiscono report e RTL generati.
#  ATTENZIONE: deve coincidere con il campo "work_dir" di vitis-comp.json,
#  altrimenti la GUI e la riga di comando costruirebbero in due posti diversi e
#  ti ritroveresti a guardare report vecchi senza capire perche'.
WORK_DIR  := $(COMPONENT)

#  Dove il tool deposita l'RTL generato, dopo la sintesi.
RTL_VHDL  := $(COMP_DIR)/$(WORK_DIR)/hls/syn/vhdl
RTL_VLOG  := $(COMP_DIR)/$(WORK_DIR)/hls/syn/verilog
REPORTS   := $(COMP_DIR)/$(WORK_DIR)/hls/syn/report


# =============================================================================
#  BERSAGLI
# =============================================================================

.PHONY: help workspace gui csim csynth cosim ip entity clean distclean

## help: elenco dei bersagli (default)
help:
	@echo ""
	@echo "  make workspace  crea/aggiorna il workspace Vitis e il component HLS"
	@echo "  make gui        apre la GUI Vitis sul workspace"
	@echo "  make csim       C Simulation      (GUI: HLS component > C SIMULATION > Run)"
	@echo "  make csynth     C Synthesis       (GUI: ... > C SYNTHESIS > Run)"
	@echo "  make cosim      C/RTL Cosimulation(GUI: ... > C/RTL COSIMULATION > Run)"
	@echo "  make ip         Package come IP   (GUI: ... > PACKAGE > Run)"
	@echo "  make entity     mostra la entity VHDL generata (dopo csynth)"
	@echo "  make clean      cancella i risultati di build, tiene il workspace"
	@echo "  make distclean  cancella anche il workspace"
	@echo ""

## workspace: crea il workspace e il component (idempotente, si puo' rilanciare)
workspace:
	$(SETUP_VITIS) && vitis -s scripts/create_workspace.py

## gui: apre la GUI Vitis
gui:
	$(SETUP_VITIS) && vitis -w $(WORKSPACE) &

# -----------------------------------------------------------------------------
#  Le tre fasi di verifica/sintesi.
#
#  Nota la simmetria con la GUI: nel pannello del component ci sono esattamente
#  queste voci, nello stesso ordine. Cliccare "Run" sotto C SIMULATION e
#  lanciare "make csim" eseguono lo stesso comando.
#
#  Perche' il "cd" nella cartella del component? Perche' dentro hls_config.cfg
#  i percorsi dei sorgenti sono relativi al file stesso (../../src/...).
#  Entrando nella cartella, i percorsi tornano.
# -----------------------------------------------------------------------------

## csim: compila ed esegue il testbench sul PC (nessun hardware coinvolto)
csim:
	$(SETUP_VITIS) && cd $(COMP_DIR) && \
	vitis-run --mode hls --csim --config $(CFG) --work_dir $(WORK_DIR)

## csynth: sintetizza il C in RTL
csynth:
	$(SETUP_VITIS) && cd $(COMP_DIR) && \
	v++ -c --mode hls --config $(CFG) --work_dir $(WORK_DIR)

## cosim: riesegue il testbench contro l'RTL vero, in simulatore
cosim:
	$(SETUP_VITIS) && cd $(COMP_DIR) && \
	vitis-run --mode hls --cosim --config $(CFG) --work_dir $(WORK_DIR)

## ip: impacchetta l'RTL come IP per il catalogo Vivado
ip:
	$(SETUP_VITIS) && cd $(COMP_DIR) && \
	vitis-run --mode hls --package --config $(CFG) --work_dir $(WORK_DIR)

# -----------------------------------------------------------------------------
#  Scorciatoia didattica: dopo la sintesi, stampa la entity VHDL generata.
#
#  E' il file da cui si capisce davvero cosa hanno prodotto i pragma: le porte
#  che vedi li' sono i fili veri del modulo.
# -----------------------------------------------------------------------------
## entity: stampa la entity VHDL del modulo top generato da HLS
entity:
	@if [ ! -f $(RTL_VHDL)/$(COMPONENT).vhd ]; then \
		echo "RTL non trovato. Lancia prima:  make csynth"; exit 1; \
	fi
	@echo "=== $(RTL_VHDL)/$(COMPONENT).vhd ==="
	@sed -n '/^entity $(COMPONENT) is/,/^end;/p' $(RTL_VHDL)/$(COMPONENT).vhd

## clean: cancella i risultati di build ma tiene il workspace
clean:
	rm -rf $(COMP_DIR)/$(WORK_DIR)
	rm -rf $(COMP_DIR)/logs

## distclean: cancella tutto il workspace (si rigenera con make workspace)
distclean:
	rm -rf $(WORKSPACE)
