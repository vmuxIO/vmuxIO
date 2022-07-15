HOST_IMAGE = /scratch/$(USER)/images/host.qcow2
HOST_IMAGE_SIZE = 20G

HOST_EXTKERN_IMAGE = /scratch/$(USER)/images/host-extkern.qcow2

GUEST_IMAGE = /scratch/$(USER)/images/guest.qcow2
GUEST_IMAGE_SIZE = 10G

QEMU_DIR = ~/qemu
QEMU_BUILD_DIR = ~/qemu-build
# QEMU_BIN = $(QEMU_BUILD_DIR)/qemu-system-x86_64

.PHONY: qemu
qemu:
	cd $(QEMU_BUILD_DIR) && \
	nix-shell '<nixpkgs>' -A qemu --run \
		"$(QEMU_DIR)/configure \
			--target-list=x86_64-softmmu \
			--enable-debug \
			--disable-alsa \
			--disable-auth-pam \
			--disable-bzip2 \
			--disable-cocoa \
			--disable-coreaudio \
			--disable-curses \
			--disable-docs \
			--disable-dsound \
			--disable-gettext \
			--disable-glusterfs \
			--disable-gtk \
			--disable-jack \
			--disable-libusb \
			--disable-libxml2 \
			--disable-lzfse \
			--disable-oss \
			--disable-pa \
			--disable-rbd \
			--disable-sdl \
			--disable-sdl-image \
			--disable-spice \
			--disable-spice-protocol \
			--disable-smartcard \
			--disable-usb-redir \
			--enable-virtfs \
			--disable-virtiofsd \
			--disable-vnc \
			--disable-vnc-jpeg \
			--disable-vnc-png \
			--disable-vnc-sasl \
			--disable-xen \
			--disable-xen-pci-passthrough \
			--disable-xkbcommon \
			--disable-bsd-user \
			--disable-libssh \
			--disable-bochs \
			--disable-cloop \
			--disable-dmg \
			--disable-qcow1 \
			--disable-vdi \
			--disable-vvfat \
			--disable-qed \
			--disable-parallels \
		&& \
		make -j 128"
			# --disable-opengl \
		# && \
		# make -j 128 clean \

.PHONY: host-image
host-image: $(HOST_IMAGE)

$(HOST_IMAGE):
	nix build -o images/nixos-host '.#host-image'
	cp images/nixos-host/nixos.qcow2 $(HOST_IMAGE)
	chmod 0644 $(HOST_IMAGE)
	qemu-build/qemu-img resize $(HOST_IMAGE) $(HOST_IMAGE_SIZE)

.PHONY: host-extkern-image
host-extkern-image: $(HOST_EXTKERN_IMAGE)

$(HOST_EXTKERN_IMAGE):
	nix build -o images/nixos-extkern-host '.#host-extkern-image'
	cp images/nixos-extkern-host/nixos.qcow2 $(HOST_EXTKERN_IMAGE)
	chmod 0644 $(HOST_EXTKERN_IMAGE)
	qemu-build/qemu-img resize $(HOST_EXTKERN_IMAGE) $(HOST_IMAGE_SIZE)

.PHONY: guest-image
guest-image: $(GUEST_IMAGE)

$(GUEST_IMAGE):
	nix build -o images/nixos-guest '.#guest-image'
	cp images/nixos-guest/nixos.qcow2 $(GUEST_IMAGE)
	chmod 0644 $(GUEST_IMAGE)
	qemu-build/qemu-img resize $(GUEST_IMAGE) $(GUEST_IMAGE_SIZE)

.PHONY: host
host:
	qemu-build/qemu-system-x86_64 \
	    -s \
	    -nographic \
	    -machine pc \
	    -cpu host \
	    -smp 128 \
	    -m 131072 \
	    -enable-kvm \
	    -drive id=root,format=qcow2,file=$(HOST_IMAGE),if=none,cache=none \
	    -device virtio-blk-pci,id=rootdisk,drive=root \
	    -virtfs local,path=/home/gierens,security_model=none,mount_tag=home \
	    -netdev tap,vhost=on,id=admin0,ifname=tap-gierens0,script=no,downscript=no \
	    -device virtio-net-pci,id=admif,netdev=admin0,mac=52:54:00:00:00:01 \
	    # -serial stdio
	    # -kernel /home/gierens/linux/arch/x86/boot/bzImage \
	    # -initrd /home/gierens/initrd \
	    # -append 'console=ttyS0' \
	    # -cdrom /home/gierens/images/host_init.iso \
	    # -drive id=root,format=raw,file=/home/gierens/images/vm_host.img,if=none,cache=none

.PHONY: host-extkern
host-extkern:
	qemu-build/qemu-system-x86_64 \
		-kernel /home/gierens/linux/arch/x86/boot/bzImage \
		-append "root=/dev/vda1 console=hvc0 nokaslr" \
		-serial null \
		-device virtio-serial \
    	-chardev stdio,mux=on,id=char0,signal=off \
    	-mon chardev=char0,mode=readline \
    	-device virtconsole,chardev=char0,id=vmsh,nr=0 \
	    -s \
	    -nographic \
	    -machine pc \
	    -cpu host \
	    -smp 128 \
	    -m 131072 \
	    -enable-kvm \
	    -drive id=root,format=qcow2,file=$(HOST_EXTKERN_IMAGE),if=none,cache=none \
	    -device virtio-blk-pci,id=rootdisk,drive=root \
	    -virtfs local,path=/home/gierens,security_model=none,mount_tag=home \
	    -netdev tap,vhost=on,id=admin0,ifname=tap-gierens0,script=no,downscript=no \
	    -device virtio-net-pci,id=admif,netdev=admin0,mac=52:54:00:00:00:01 \
	    # -serial stdio
	    # -kernel /home/gierens/linux/arch/x86/boot/bzImage \
	    # -initrd /home/gierens/initrd \
	    # -append 'console=ttyS0' \
	    # -cdrom /home/gierens/images/host_init.iso \
	    # -drive id=root,format=raw,file=/home/gierens/images/vm_host.img,if=none,cache=none
		# incremental build section
		#-kernel ./linux/arch/x86/boot/bzImage \
		#-append "root=/dev/vda console=hvc0 nokaslr" \
		#-serial null \
		#-device virtio-serial \
    	#-chardev stdio,mux=on,id=char0,signal=off \
    	#-mon chardev=char0,mode=readline \
    	#-device virtconsole,chardev=char0,id=vmsh,nr=0 \

.PHONY: debug-host
debug-host:
	cd linux && \
	gdb \
		-ex 'target remote :1234' \
		-ex 'set confirm off' \
		-ex 'add-symbol-file vmlinux' \
		-ex 'set confirm on'

.PHONY: guest
guest:
	ip link show tap0 2>/dev/null || (tunctl -t tap0 && brctl addif br0 tap0 && ip link set tap0 up)
	/home/gierens/qemu-build/qemu-system-x86_64 \
	    -nographic \
	    -machine microvm \
	    -cpu host \
	    -smp 4 \
	    -m 4096 \
	    -enable-kvm \
	    -drive id=root,format=qcow2,file=/home/gierens/images/guest.qcow2,if=none,cache=none \
	    -device virtio-blk-device,id=rootdisk,drive=root \
	    -netdev tap,vhost=on,id=admin0,ifname=tap0,script=no,downscript=no \
	    -device virtio-net-device,id=admif,netdev=admin0,mac=52:54:00:00:00:02,use-ioregionfd=true
		# -serial none \
        # -monitor stdio \

.PHONY: linux-shell
linux-shell:
	nix build -o ./linux-shell  github:Mic92/vmsh#kernel-deps
	# nixpkgs#ctags nixpkgs#cscope nixpkgs#bear
	./linux-shell/bin/linux-kernel-build bash

.PHONY: linux-clean
linux-clean:
	echo "things like: "
	echo "cd ./linux"
	echo "make mrproper # also cleans config"
	echo "make clean # cleans most binaries"
	echo "rm tools/objtool/fixdep tools/objtool/objtool # cleans tools that are not cleaned by make"
