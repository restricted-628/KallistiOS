/* Link-only overlay fixture: initialized data and an exact four-byte BSS. */
int overlay_state;
int overlay_value = 7;

int overlay_entry(void) {
    return ++overlay_state + overlay_value;
}
