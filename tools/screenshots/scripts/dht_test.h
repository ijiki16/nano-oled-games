#define SCRIPT_FRAMES 20
uint8_t script(uint32_t) { return 0; }
void after(uint32_t f) { if (f == 10) shot("dht_test"); }
