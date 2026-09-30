#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <uv.h>

uv_loop_t *loop;
uv_async_t async;

_Atomic double percentage;

void fake_download(uv_work_t *req) {
    int size = *((int*) req->data);
    int downloaded = 0;
    double pct;
    while (downloaded < size) {
        pct = downloaded * 100.0 / size;
        atomic_store_explicit(&percentage, pct, memory_order_release);
        uv_async_send(&async);

        sleep(1);
        downloaded += (200+random())%1000; // can only download max 1000bytes/sec,
                                           // but at least a 200;
    }
    // Ensure final 100% progress update is sent
    pct = 100.0;
    atomic_store_explicit(&percentage, pct, memory_order_release);
    uv_async_send(&async);
}

void after(uv_work_t *req, int status) {
    fprintf(stderr, "Download complete\n");
    uv_close((uv_handle_t*) &async, NULL);
}


void print_progress(uv_async_t *handle) {
    double pct = atomic_load_explicit(&percentage, memory_order_acquire);
    fprintf(stderr, "Downloaded %.2f%%\n", pct);
}

int main() {
    loop = uv_default_loop();

    uv_work_t req;
    int size = 10240;
    req.data = (void*) &size;

    uv_async_init(loop, &async, print_progress);
    uv_queue_work(loop, &req, fake_download, after);
    return uv_run(loop, UV_RUN_DEFAULT);
}
