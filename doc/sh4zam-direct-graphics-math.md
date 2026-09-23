# Direct SH4ZAM graphics math

The new graphics stack uses the official SH4ZAM v0.9.0 submodule directly.
The upstream source and its authorship/history are unchanged. This pass does
not remove the public legacy KOS math API or rewrite unrelated old examples.

## Integration changes

- Collision dot products, cross products, reciprocal square roots, and the
  triangle threshold square root use SH4ZAM instead of KOS `fmath` or libm.
- Camera application and matrix-stack operations call SH4ZAM's unaligned
  XMTRX load/store/apply routines directly against the existing public storage.
- Camera, hierarchy, and skeleton composition use an unchecked SH4ZAM storage
  bridge after their inputs have been admitted. Public `mat_compose` still
  validates its own arguments. Four one-off vector transforms preserve XMTRX
  and output/input aliasing; substituting `shz_mat4x4_mult` would change that
  accelerator-state contract.
- Animation, camera, frustum, projection, deformation, lighting, toon, cell,
  tilemap, sprite, and particle math no longer choose independent scalar host
  implementations with `__DREAMCAST__`. SH4ZAM selects its backend. The hardware
  guards around actual TA/scene operations remain.
- Normalization reuses the squared length already computed for validation in
  camera, quaternion, lighting, toon, and particle paths. It does not call a
  normalize helper that recomputes that length.
- Five new graphics demos use SH4ZAM trigonometry; the toon demo obtains sine
  and cosine of the same angle with one `shz_sincosf` call.

Host geometry tests now link the official software XMTRX state. Camera and
matrix-stack tests exercise that state rather than mocking legacy `mat_*`
calls. `utils/Makefile.sh4zam-state` supplies the shared object to consumers.

## Checks and compiler effects

No broad `memory` clobber or new inline assembly is added. SH4ZAM owns the
instruction-level register constraints. Existing store-queue/DMA ordering
barriers are not math overhead and are not removed by this change.

Public input, capacity, finite-value, overflow, and alias contracts remain.
The unchecked composition bridge avoids repeating pointer/alignment checks
inside already-validated traversals. Immutable/prepared Compact paths retain
their existing admission model. Transactional builders still need their
preflight passes where later invalid input must leave caller output unchanged;
this change does not claim every checked API has become an unchecked hot loop.

Existing `matrix_t` and `vector_t` public ABI storage is retained. Matrix
imports use alias-safe copies into real SH4ZAM objects, not incompatible struct
pointer casts. Domain-specific scalar arithmetic, including the scaled robust
inverse-transpose normal-matrix construction, is not a legacy KOS math call
and is not replaced with an operation having different failure semantics.

## Pending specular power decision

The one explicit libm transcendental exception is the runtime specular
`powf` call in `pvr_lighting.c`, pending upstream guidance. This is standard
libm, not KOS `fmath`. The supported shininess interval is [1, 128], including
noninteger exponents. SH4ZAM 0.9.0 documents `shz_powf` as an approximation,
and its runtime path does not preserve the unit endpoint at these exponents.
An isolated host probe on GCC 14 and Apple Clang produced:

| Runtime expression | Mathematical result | `shz_powf` result |
| --- | ---: | ---: |
| 1 raised to 2 | 1 | 1.0573 |
| 1 raised to 32 | 1 | 3.55286 |
| 1 raised to 128 | 1 | 163.539 |

Compile-time constant arguments can take a different builtin path. These
numbers are host evidence, not SH-4 hardware measurements. The lighting
regression test checks unchanged unit specular response across representative
integer and fractional shininess values. No upstream fix or intended error
budget is assumed while waiting for the maintainer's response.

## Validation

- GCC 16.2 SH-4 KOS build and forced integration-example rebuild: passed.
- All five updated graphics examples build and link.
- GCC 14 GNU17 full host sweep: 68 suites passed initially; the remaining UV
  asset suite exposed a missing software-XMTRX link input in its Makefile.
  After correction, that suite passed on GCC GNU17/C23 and Clang GNU17/C2x.
- Eleven focused suites passed in GCC 14 C23, Apple Clang 16 GNU17, and Clang
  C2x, including camera/compose/stack, collision/animation, geometry, lighting,
  toon, cell, particle, and tilemap tests.
- Eleven focused suites passed Clang AddressSanitizer and UndefinedBehaviorSanitizer:
  camera/compose/stack, collision/animation, deformation, geometry, lighting,
  toon, particle, and tilemap.
- The SH4ZAM source checker and all 11 checker regression tests passed. The
  upstream submodule remains clean at the official v0.9.0 commit.
- SH-4 disassembly retains FIPR/FSRRA instructions. The changed math objects
  have no unresolved legacy `mat_load`, `mat_store`, `mat_apply`, `mat_compose`,
  `fipr`, or `frsqrt` calls. Camera descriptor-builder calls remain intentional.
- Isolated MMU-capable Flycast, 16 MiB: the rebuilt integration test passed
  in both interpreter and dynarec modes, covering XMTRX preservation,
  composition aliasing, skinning, admitted Compact/wire/modifier/two-volume/
  toon draws, and thread FPU state. Toon and skin demos passed in dynarec mode.
- The lighting regression executable, linked against the new SH-4 library,
  passed in both interpreter and dynarec modes, including the specular test.

Physical Dreamcast performance and precision remain separate validation gates.
No console benchmark or claim of a measured hardware speedup is made here.
