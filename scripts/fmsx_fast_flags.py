"""Flags de compilacao rapidos APENAS para o nucleo fMSX (src/fmsx/).

O framework Arduino-ESP32 2.0.x compila todo o projeto com
-fno-jump-tables e -fno-tree-switch-conversion. Isso existe para codigo
que roda com o cache da flash desligado (ISRs em IRAM), pois as jump
tables ficam na .rodata em flash. No nucleo do emulador esses flags
transformam o switch de 256 opcodes do Z80 (e os prefixos CB/ED/DD/FD,
alem dos switch de portas de I/O) em arvores de comparacao com cerca
de 8 desvios por instrucao emulada.

O codigo de src/fmsx/ roda somente na tarefa do emulador (nunca em ISR)
e o app nao grava na flash durante a emulacao, entao as jump tables
podem ser reativadas com seguranca nesses arquivos. FabGL, HAL e o
restante do firmware continuam com os flags originais do framework.

Tambem remove -fstack-protector dos arquivos do nucleo: as funcoes de
renderizacao de linha (buffers ZBuf[320]) e de audio (Buf[256]) pagavam
o canario de pilha a cada chamada (centenas de vezes por quadro).
"""
Import("env")

_REMOVE = {"-fno-jump-tables", "-fno-tree-switch-conversion", "-fstack-protector"}
_ADD = ["-fjump-tables", "-ftree-switch-conversion", "-fno-stack-protector"]


def _fmsx_fast(env, node):
    path = node.get_path().replace("\\", "/")
    if "/src/fmsx/" not in "/" + path and not path.startswith("src/fmsx/"):
        return node
    flags = [f for f in env.get("CCFLAGS", []) if str(f) not in _REMOVE]
    return env.Object(node, CCFLAGS=flags + _ADD)


env.AddBuildMiddleware(_fmsx_fast)
print("[fmsx_fast_flags] jump tables reativadas somente em src/fmsx/")
