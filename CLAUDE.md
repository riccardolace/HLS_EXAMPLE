# CLAUDE.md

Progetto didattico: costruzione incrementale di una IP AXI4-Stream + AXI4-Lite in
Vitis HLS 2025.2, fino all'inserimento nel catalogo IP di Vivado.

## ⚠️ Il metodo ha priorità sul risultato

L'utente **sta imparando HLS** venendo da VHDL/hardware. Il codice non è il
deliverable: la comprensione lo è. Tre regole vincolanti, concordate esplicitamente:

1. **Costruzione a strati.** Ogni gradino aggiunge **una cosa sola**, poi si
   sintetizza e si guarda *cosa è cambiato nell'RTL generato*. Il concetto si
   vede prima di essere spiegato. Non anticipare gradini successivi.
2. **Fermarsi dopo ogni gradino.** Consegnare, spiegare cosa è cambiato,
   e aspettare le domande. Non proseguire di iniziativa.
3. **GUI-first.** L'utente lavora nella GUI Vitis; la CLI serve per
   riproducibilità e debug. Ogni operazione va documentata in entrambe le forme,
   affiancate.

**Convenzioni dei sorgenti** (sono parte della documentazione, non solo codice):
commenti in italiano; ogni `#pragma` annotato con cosa produce fisicamente
(porte, registri, FSM); confronti espliciti col VHDL dove esiste un corrispettivo;
il *perché* di ogni scelta non ovvia; nessuna riga "magica" non spiegata.

Lo **stato di avanzamento** è la tabella della scala didattica in `README.md`
(colonna "Stato") — unica fonte di verità, va aggiornata a ogni gradino chiuso.

## Comandi

```bash
make              # elenco dei bersagli
make workspace    # crea/aggiorna il workspace Vitis (idempotente)
make gui          # vitis -w workspace
make csim         # C Simulation      (GUI: C SIMULATION > Run)
make csynth       # C Synthesis       (GUI: C SYNTHESIS > Run)
make cosim        # C/RTL Cosimulation
make ip           # Package come IP di catalogo
make entity       # stampa la entity VHDL generata (dopo csynth)
make clean        # cancella i build, tiene il workspace
```

Non si sintetizza finché `make csim` non è verde.

## Ambiente (verificato, non assumere altro)

| Fatto | Conseguenza |
|---|---|
| Vivado + Vitis **2025.2** in `/tools/Xilinx/2025.2` | `make` fa il `source settings64.sh` da solo |
| **Il binario `vitis_hls` non esiste più** | Da CLI: `v++ -c --mode hls --config`, `vitis-run --mode hls --csim/--cosim/--package`, `vitis -s script.py`. I tutorial con `vitis_hls -f run_hls.tcl` vanno tradotti |
| Installate **solo famiglie Versal**, **zero board file** | Target `xcvc1902-vsva2197-2MP-e-S` (VCK190). Progetti "a part", non "a board" |
| `xilinx_vck190_base_202520_1` presente con `hw_emu` | Piattaforma per la Fase 6 |
| **XRT e rootfs/sysroot Versal assenti** | L'hardware emulation richiederà il download di `xilinx-versal-common-v2025.2` (~2-3 GB) |
| Clock del progetto: `clock=4.0` = **4 ns**, non MHz | 250 MHz |

## Struttura

```text
src/         sorgenti sintetizzati        (syn.file)
tb/          testbench, gira sul PC       (tb.file) — deve essere auto-verificante
scripts/     create_workspace.py
workspace/   progetto Vitis — RIGENERABILE, in .gitignore
docs/        00_hls_vs_vhdl.md, 02_flusso_build.md
```

I sorgenti stanno **fuori** dal workspace, referenziati con percorsi relativi in
`hls_config.cfg` (relativi al file cfg stesso).

Output dopo `make csynth`, sotto `workspace/axis_scaler/axis_scaler/hls/`:
`syn/vhdl/axis_scaler.vhd` (RTL VHDL — il file da leggere a ogni gradino),
`syn/report/axis_scaler_csynth.rpt`, `impl/ip/` (IP + driver C),
`impl/ip/drivers/*/src/*_hw.h` (mappa registri generata — **mai copiarla a mano**).

## Trappole già incontrate

- **`flow_target`**: il wizard GUI "New HLS Component" scrive di default
  `flow_target=vitis` + `package.output.format=xo` (kernel per v++). Per una IP di
  catalogo Vivado servono `flow_target=vivado` + `package.output.format=ip_catalog`.
  `create_workspace.py` lo imposta già bene.
- **Include del testbench**: `tb/` include un header di `src/`, serve
  `tb.cflags=-I../../src`. Le chiavi valide sono `syn.cflags`, `syn.file_cflags`,
  `tb.cflags`, `tb.file_cflags`.
- **Auto-pipeline**: dalla 2020.2 HLS mette in pipeline i loop da solo. Il gradino
  1.1 ha già II=1 senza alcun `#pragma HLS PIPELINE`. Non promettere esperimenti
  del tipo "aggiungiamo PIPELINE e confrontiamo": non cambierebbe nulla.
- **`hls::stream<ap_uint<32>>`** genera solo TDATA/TVALID/TREADY, senza TLAST →
  inutilizzabile con AXI DMA. Usare sempre `ap_axis<W,0,0,0>`.
- **`--work_dir`** deve coincidere con `work_dir` in `vitis-comp.json`
  (`axis_scaler`), altrimenti GUI e CLI costruiscono in due posti diversi.
- `vitis-run`/`v++`/`config_*` **non supportano `-h`**. Per scoprire le opzioni
  valide: passare un flag inesistente (es. `config_cosim -zzz`) e leggere l'elenco
  che stampa l'errore.

## Corrispondenza `hls_config.cfg` ↔ Tcl legacy

Il comando Tcl `config_<gruppo> -<opzione> <valore>` corrisponde alla chiave
`<gruppo>.<opzione>=<valore>` con prefisso di sezione (`syn.`, `cosim.`,
`package.`). Tabella completa in `docs/02_flusso_build.md`.

## Riferimenti

Il metodo generale, indipendente da questo progetto, è estratto nella skill
personale `tutorial-xilinx-skill` (`~/.claude/skills/`). Questo CLAUDE.md ne è
l'applicazione a questo progetto: **per continuare qui basta questo file**,
la skill serve quando si apre un tema nuovo (AI Engine, Vivado, PetaLinux...).

## Piano completo

`~/.claude/plans/ciao-in-questa-cartella-wobbly-nova.md` — specifica dell'IP
finale (mappa registri, interfacce), scala didattica completa 1.1→1.9, e fasi
successive (report, esperimenti, cosim, export, block design con AXI VIP,
extensible platform + hw emulation).
