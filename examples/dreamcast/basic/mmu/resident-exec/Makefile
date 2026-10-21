# OUT and DISC_DIR must be new directories. Source this checkout's environ.sh.
all:
	python3 build.py --out "$(OUT)"
disc: all
	python3 make-disc.py "$(OUT)/resident-exec.bin" "$(DISC_DIR)" --members "$(OUT)" --scramble "$(KOS_BASE)/utils/scramble/scramble"
