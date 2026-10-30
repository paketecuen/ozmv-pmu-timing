# Build the manuscript and its figures.
#   make          the figures (the manuscript is not in this repository)
#   make figures  figures/src/fig_*.py (matplotlib, text by LaTeX) and figures/src/*.tex (TikZ)
#                 -> figures/out/*.pdf, with PNG previews in figures/preview/
#   make clean
FIGPY  := $(wildcard figures/src/fig_*.py)
FIGTEX := $(wildcard figures/src/*.tex)

all: figures

figures:
	@for f in $(FIGPY); do echo "figure $$f"; (cd figures/src && python3 $$(basename $$f)) || exit 1; done
	@for f in $(FIGTEX); do n=$$(basename $$f .tex); echo "figure $$f"; \
	  (cd figures/src && pdflatex -interaction=nonstopmode -halt-on-error -output-directory=../out $$n.tex > /dev/null) || exit 1; \
	  rm -f figures/out/$$n.aux figures/out/$$n.log; done
	@mkdir -p figures/preview
	@for f in figures/out/*.pdf; do pdftoppm -png -r 200 -singlefile $$f figures/preview/$$(basename $$f .pdf); done

clean:
	rm -rf figures/preview

.PHONY: all figures clean
