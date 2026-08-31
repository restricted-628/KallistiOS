# Compact volume assets

This example opens one pointer-free compact volume section, validates its
indices against an admitted model, and iterates the section through the same
triangle decoder used by modifier-volume rendering. The retained three-word
triangle payload demonstrates application-defined collision metadata.

The section image would ordinarily be emitted inside PCM2 by the host model
compiler. It is embedded here so the example has no external asset dependency.
