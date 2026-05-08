.PHONY: help build-baremetal build-linux test-host clean setup

help:
	@echo "MiniJV880 Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  setup              - Initialize git submodules"
	@echo "  build-baremetal    - Build bare-metal kernel (Circle)"
	@echo "  build-linux        - Build Linux version (buildroot)"
	@echo "  test-host          - Run host synth render smoke test"
	@echo "  deploy-baremetal   - Deploy bare-metal kernel via WiFi TFTP"
	@echo "  deploy-samples     - Copy samples to SD card"
	@echo "  clean              - Remove build artifacts"

setup:
	git submodule update --init --recursive

build-baremetal:
	cd platforms/baremetal && make clean && make

build-linux:
	cd platforms/linux && make

test-host:
	$(CC) -std=c99 -Wall -Wextra -Isynth/include -Isynth/baremetal \
		synth/tests/smoke_render.c \
		synth/baremetal/voice.c \
		synth/baremetal/tone.c \
		synth/baremetal/wg.c \
		synth/baremetal/tvf.c \
		synth/baremetal/tva.c \
		synth/baremetal/envelope.c \
		synth/baremetal/lfo.c \
		synth/baremetal/effects.c \
		-o builds/smoke_render
	./builds/smoke_render

deploy-baremetal:
	cd platforms/baremetal && make deploy

deploy-samples:
	cd platforms/baremetal && ./deploy-to-sd.sh

clean:
	cd platforms/baremetal && make clean
	cd platforms/linux && make clean 2>/dev/null || true
