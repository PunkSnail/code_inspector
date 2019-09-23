# 代码流程检查员

- 基于单包的代码处理流程, 检查多包处理流程的的不同

## 出生理由
单包与多包的处理逻辑一致, 在高密度 IO 的情况下, 同时处理多个包可显著减少 cache miss, 提高性能(典型如 DPDK)
```c
    while (n > 4)
    {
        x0 = x1 =x2= x3 = z;
        count += 4;

        func_a( x0 );
        func_a(x1);
        func_a(x2);
        func_a(x3);

        func_b(a, b0, b1, b2, b3,
               c0, c1, c2, c3);
        ...
    }
    while (n > 2)
    {
        x0 = x1 = z;
        count += 2;

        func_a( x0 );
        func_a(x1);

        func_b(a, b0, b1, 
               c0, c1);
        ...
    }
    while (n > 0)
    {
        x0 = z;
        count += 1;

        func_a(x0);
        func_b(a, b0, c0);
        ...
    }
```
于是有了类似于上面这种形式的代码, 编写和修改时容易出错, 本工具的目的就是为了辅助程序员检查多包处理代码 <s>也许最终可以实现自动生成多包处理代码的工具</s>

