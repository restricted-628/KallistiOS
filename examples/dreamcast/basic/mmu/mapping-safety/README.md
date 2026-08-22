# MMU mapping safety probe

This regression exercises the checked KOS MMU page lifecycle without
deliberately causing an address exception. It verifies argument rejection,
cache-policy encoding, physical-to-virtual lookup, live remapping after a TLB
fill, unmapping, remapping after reclamation, and destruction of the active
context. It also verifies invalid ASIDs, virtual-range overflow, static physical
address truncation, and all-or-nothing cache-policy updates.

Success prints `KOSMMUMAP remap=1 cache=1 unmap=1 destroy=1 translation=N`.
`translation=1` confirms that the runtime applied general P0 translation;
`translation=0` means the structural lifecycle passed but translated data
access remains a real-hardware validation gate. Some emulators implement only
the store-queue-specific MMU path for this guest and therefore report zero.
