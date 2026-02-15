```c
int foo() {
  return bar() + 1;
}
```

```s
push   %rax
callq  bar
add    $0x1,%eax
pop    %rcx
retq
```

```s
mov    (%rsp),%r10
```

`mov` salva la cima dello stack in `%r10`

```s
xor    %r11,%r11
addq   $0x8,%gs:(%r11)
```

`[GS_Base] += 8`

```s
mov    %gs:(%r11),%r11
mov    %r10,%gs:(%r11)
push   %rax
callq  bar
add    $0x1,%eax
pop    %rcx
xor    %r11,%r11
mov    %gs:(%r11),%r10
mov    %gs:(%r10),%r10
subq   $0x8,%gs:(%r11)
cmp    %r10,(%rsp)
jne    trap
retq

trap:
ud2
```

