# vNerveBiLiveReceptor

[![Project Status: WIP – Initial development is in progress, but there has not yet been a stable, usable release suitable for the public.](https://www.repostatus.org/badges/latest/wip.svg)](https://www.repostatus.org/#wip)

## TODO

### Worker

- [ ] **Parse json to protobuf**.
- [x] Pack header and serialized protobuf.
- [x] ~~Introduce ZeroMQ and send to supervisor.~~ Use custom protocol to communicate between supervisor and worker.
- [ ] Use thread local storage instead of boost::thread_specific_ptr.
- [ ] Refactor(class/namespace/filename).

### Supervisor

- [x] Drop gRPC.
- [x] Event loop, weighted round robin.
- [ ] ...
