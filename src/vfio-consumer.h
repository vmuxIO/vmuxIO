
#define VFIOC_SECRET 1337

typedef struct {
  int placeholder;
} vfio_consumer_t;

int vfioc_init(vfio_consumer_t *self);
