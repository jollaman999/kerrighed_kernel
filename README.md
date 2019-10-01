## Centos 의 Kerrighed 패치

HCC 서버의 스펙때문에 특히 Xeon CPU를 구버전(2.6.30 kerrighed kernel)
이 지원하지 않아서 해당 서버에서 설치가 가능한 Centos 6.10 으로 설치를 진행후
`linux-2.6.32-754.22.1.el6` 의 커널 소스를 받아 kerrighed 패치 진행.

하지만 패치 과정에서 Centos 에서 수정한 부분이 있기 때문에 해당 부분들은
수동으로 수정해줘야 한다.

## 10/01

```shell
[Patching]
 * Complete
    ./mm/swapfile.c
    ./mm/vmscan.c
 * Working
 
[etc]
git ignore update
 * ignore vscode workspace file
```