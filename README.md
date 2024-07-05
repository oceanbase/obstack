# 功能
通用的快速堆栈生成工具

# 特点
* 阻塞窗口小, 线程平均阻塞时间<1ms
* 效率高，通常可以在几秒内输出带完整行号的结果
* 支持解析动态库函数名
* 支持堆栈聚合
* 支持指定二进制与debuginfo路径
* 支持抓独立线程

# build

```
bash build.sh release
cd build_release && make -j4
```
# binary path
```
./build_release/src/obstack
```
# usage
## basic
```
./obstack $pid
```
## more
```
./obstack --help
```