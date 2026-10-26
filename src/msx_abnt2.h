#ifndef MSX_ABNT2_H
#define MSX_ABNT2_H
/* ABNT2 -> matriz MSX (linhas 0..2). Usado apenas quando o F10 ativa BR.
 * O mapeamento e guiado pelas tabelas de teclado da BIOS ativa em 0DA5h
 * (normal), 0DD5h (Shift), 0E05h (Graph) e 0E35h (Graph+Shift).
 * Expert 1.1 e Hotbit 1.2 possuem teclas mortas nas matrizes distintas.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    uint8_t normal[24], shifted[24], graph[24], graph_shifted[24];
    uint8_t from_bios;
} MSXABNT2Table;

typedef struct {
    uint8_t row, column, shift, graph;
    uint8_t valid;
    uint8_t code;  /* MSX CODE: reservado a combinacoes que o requerem */
} MSXABNT2Result;

enum MSXABNT2Physical {
    BR_QUOTE, BR_0, BR_1, BR_2, BR_3, BR_4, BR_5,
    BR_6, BR_7, BR_8, BR_9, BR_MINUS, BR_EQUALS,
    BR_ACUTE, BR_LEFTBRACKET, BR_CEDILLA, BR_TILDE,
    BR_RIGHTBRACKET, BR_BACKSLASH, BR_COMMA, BR_PERIOD,
    BR_SEMICOLON, BR_SLASH, BR_PHYSICAL_COUNT
};

static inline void msx_abnt2_init_table(MSXABNT2Table *t)
{
    static const uint8_t normal[24] = {
        '0','1','2','3','4','5','6','7',
        '8','9','-','=','\\','[',']',';',
        '\'','`',',','.','/',0xFF,'a','b'
    };
    static const uint8_t shifted[24] = {
        ')','!','@','#','$','%','^','&',
        '*','(','_','+','|','{','}',':',
        '"','~','<','>','?',0xFF,'A','B'
    };
    memcpy(t->normal,normal,24);
    memcpy(t->shifted,shifted,24);
    memset(t->graph,0,24);
    memset(t->graph_shifted,0,24);
    t->from_bios=0;
}

static inline int msx_abnt2_load_bios(MSXABNT2Table *t,const uint8_t *rom,size_t size)
{
    int i;
    if(!rom || size < 0xE35u+24u) return 0;
    /* A assinatura reduz o risco de confundir codigo de outra BIOS
       com tabelas de teclado (endereco diferente em algumas maquinas). */
    for(i=0;i<8;i++) if(rom[0x0DA5+i]!=(uint8_t)('0'+i)) return 0;
    if(rom[0x0DA5+22]!='a' || rom[0x0DA5+23]!='b') return 0;
    memcpy(t->normal,rom+0x0DA5,24);
    memcpy(t->shifted,rom+0x0DD5,24);
    memcpy(t->graph,rom+0x0E05,24);
    memcpy(t->graph_shifted,rom+0x0E35,24);
    t->from_bios=1;
    return 1;
}

/* Saidas fisicas do ABNT2 com e sem Shift; 0 indica tecla morta.
   Na fileira numerica, Shift+6 e trema (tecla morta), nao circunflexo. */
static inline uint8_t msx_abnt2_character(unsigned physical,int pc_shift)
{
    static const uint8_t plain[BR_PHYSICAL_COUNT] = {
        '\'', '0','1','2','3','4','5','6','7','8','9','-','=',
        0,'[',0x87,0,']','\\',',','.',';','/'
    };
    static const uint8_t shifted[BR_PHYSICAL_COUNT] = {
        '"',')','!','@','#','$','%',0,'&','*','(','_','+',
        0,'{',0x80,0,'}','|','<','>',':','?'
    };
    return pc_shift ? shifted[physical] : plain[physical];
}

static inline MSXABNT2Result msx_abnt2_lookup(const MSXABNT2Table *t,
                                             int profile,unsigned physical,int pc_shift)
{
    MSXABNT2Result result={0,0,0,0,0,0};
    uint8_t ch;
    int best=999,idx,layer;
    const uint8_t *layers[4]={t->normal,t->shifted,t->graph,t->graph_shifted};
    if(physical>=BR_PHYSICAL_COUNT) return result;

    /* MSX internacional (MSX1/2/2+), somente layout BR:
       A tecla PS/2 [`´] deve usar a DEAD KEY real da BIOS, linha 2,
       coluna 5 (tecla 130 na matriz virtual). O manual da BIOS define:
            DEAD KEY          => grave (`)  (KANAST = 1)
            SHIFT + DEAD KEY  => agudo (´)  (KANAST = 2)
            CODE + DEAD KEY   => circunflexo (^) (KANAST = 3)
       Portanto, a tecla SEM Shift no PC deve ativar Shift+DEAD KEY
       no MSX para compor 'á', e a tecla COM Shift no PC deve ativar
       DEAD KEY sem modificadores para compor 'à'. Nunca usar ASCII
       literal da crase nem CODE nessa tecla. A composicao fica na BIOS.
       Expert e Hotbit seguem abaixo com suas matrizes nativas intactas.
    */
    if(profile==0)
    {
        /* Apostrofo independente da deadkey: encontrar ASCII real na
           BIOS. Nao fazer esta busca para os acentos da tecla [`´]. */
        if(physical==BR_QUOTE && !pc_shift)
        {
            const uint8_t target='\'';
            int found=-1, layer_found=-1;
            for(int layer=0;layer<4 && found<0;layer++)
            {
                const uint8_t *table=layers[layer];
                for(int idx=0;idx<24;idx++)
                    if(table[idx]==target)
                    {
                        found=idx;layer_found=layer;break;
                    }
            }
            if(found>=0)
            {
                result.row=(uint8_t)(found>>3);
                result.column=(uint8_t)(found&7);
                result.shift=(uint8_t)(layer_found&1);
                result.graph=(uint8_t)(layer_found>>1);
                result.valid=1;
                return result;
            }
        }
        unsigned pos=24;
        int msx_shift=pc_shift;
        switch(physical)
        {
            case BR_QUOTE:     pos=16; break; /* ' / " */
            case BR_SLASH:     pos=20; break; /* / / ? */
            case BR_BACKSLASH: pos=12; break; /* \ / | */
            case BR_TILDE:
                pos=pc_shift?6:17;
                msx_shift=1; /* ^ / ~ */
                break;
            case BR_ACUTE:
                pos=21;              /* Matriz MSX 2:5: DEAD KEY da BIOS */
                msx_shift=!pc_shift; /* Sem Shift = agudo; com Shift = grave */
                break;
            case BR_6:
                if(pc_shift) return result; /* trema inexistente */
                break;
            default: break;
        }
        if(pos<24)
        {
            result.row=(uint8_t)(pos>>3);
            result.column=(uint8_t)(pos&7);
            result.shift=(uint8_t)msx_shift;
            result.graph=0;
            result.code=0; /* Nao acionar CODE para agudo ou grave. */
            result.valid=1;
            return result;
        }
    }

    /* Expert 1.1 (EXPERT11.ROM): em sua tabela 0DA5h, as teclas
       de apóstrofo, barra e barra invertida não estão no mesmo lugar
       das BIOS internacionais. Usar a posição física específica em
       vez de uma busca por ASCII (que pode atingir uma tecla morta).
       Shift+6 no PC ABNT2 geraria trema, mas a BIOS Expert nao fornece
       este caractere nessa combinacao: nao emitir nenhum caractere. */
    if(profile==1)
    {
        int index=-1, shift=0, graph=0;
        switch(physical)
        {
            case BR_QUOTE:
                index=pc_shift ? 2 : 8;
                shift=1;
                break; /* MSX Shift+2 -> " ; Shift+8 -> ' */
            case BR_SLASH:
                index=21;
                shift=pc_shift;
                break; /* 2:5 /, Shift+2:5 ? */
            case BR_BACKSLASH:
                index=21;
                graph=1;
                shift=pc_shift;
                break; /* GRAPH+2:5 \, GRAPH+Shift+2:5 | */
            case BR_6:
                if(pc_shift) return result; /* Trema indisponivel */
                break;
            default:
                break;
        }
        if(index>=0)
        {
            result.row=(uint8_t)(index>>3);
            result.column=(uint8_t)(index&7);
            result.shift=(uint8_t)shift;
            result.graph=(uint8_t)graph;
            result.valid=1;
            return result;
        }
    }

    /* Hotbit 1.2: mapa fisico confirmado em HOTBIT12.ROM (32 KiB).
       As tres deadkeys sao 1:5, 1:6 e 2:0.  E importante NAO
       procurar apostrofo pelo primeiro 0xFF da tabela: no Hotbit a
       tecla 1:6 e morta sem Shift, mas produz apostrofo COM Shift.
       Os numeros e as letras permanecem na traducao da BIOS normal.
       { e } nao existem nas quatro tabelas originais do Hotbit 1.2;
       com ABNT2, o HAL insere { }, e apostrofo independente da deadkey,
       em posicoes Graph+Shift originalmente vazias na copia
       da BIOS carregada em RAM (nao na ROM do SD). */
    if(profile==2)
    {
        int index=-1, shift=0, graph=0;
        switch(physical)
        {
            case BR_QUOTE:
                /* HOTBIT12.ROM possui apostrofo REAL em Shift+1:6,
                   posicao 14 da tabela 0DD5h. Sem Shift, 1:6 eh
                   uma deadkey (0xFF).  Nao usar Graph+Shift 0:5:
                   essa entrada e sintetica e nao gera corretamente
                   apostrofo em algumas rotinas da BIOS. */
                index=pc_shift ? 6 : 14;
                shift=1; graph=0; break;
            case BR_SLASH:        index=20; shift=pc_shift; break;
            case BR_TILDE:        index=16; shift=pc_shift; break;
            case BR_ACUTE:        index=13; shift=pc_shift; break;
            case BR_BACKSLASH:    index=pc_shift ? 21 : 12;
                                   graph=pc_shift; shift=0; break;
            case BR_LEFTBRACKET:  index=pc_shift ? 1 : 17;
                                   shift=pc_shift; graph=pc_shift; break;
            case BR_RIGHTBRACKET: index=pc_shift ? 4 : 17;
                                   shift=1; graph=pc_shift; break;
            default: break;
        }
        if(index>=0)
        {
            result.row=(uint8_t)(index>>3);
            result.column=(uint8_t)(index&7);
            result.shift=(uint8_t)shift;
            result.graph=(uint8_t)graph;
            /* Shift+[ e Shift+] so sao validos se a BIOS em RAM
               foi preparada pelo HAL; nunca retornar um NUL como '{'. */
            result.valid=(uint8_t)(!pc_shift ||
                (physical!=BR_LEFTBRACKET && physical!=BR_RIGHTBRACKET) ||
                (t->graph_shifted[index]==(physical==BR_LEFTBRACKET?'{':'}')));
            return result;
        }
    }

    /* Exclusivo das BIOS brasileiras 1.1/1.2: posicoes especiais validadas
       pelas tabelas; a familia padrao nunca chega aqui com tecla morta. */
    /* BIOS brasileiras 1.1/1.2: posicoes especiais validadas pelas tabelas
       das ROMs. A BIOS trata a composicao com a vogal depois da tecla morta. */
    if(physical==BR_ACUTE || physical==BR_TILDE ||
       (physical==BR_6 && pc_shift))
    {
        int pos;
        int shift=pc_shift;
        if(physical==BR_ACUTE) pos=13; /* BIOS brasileiras: primeira tecla morta */
        else if(physical==BR_TILDE) pos=profile==2?16:15;
        else { pos=profile==2?14:13;shift=0; }
        result.row=(uint8_t)(pos>>3);
        result.column=(uint8_t)(pos&7);
        result.shift=(uint8_t)shift;
        result.valid=1;
        return result;
    }
    ch=msx_abnt2_character(physical,pc_shift);
    /* Cedilha nas ROMs brasileiras usa 87h/80h; ROM internacional pode
       nao possuir esta tecla, logo nenhuma posicao e inventada. */
    for(layer=0;layer<4;layer++)
        for(idx=0;idx<24;idx++)
            if(layers[layer][idx]==ch)
            {
                int target_shift=layer&1;
                int target_graph=(layer>>1)&1;
                int cost=target_graph*100+(target_shift!=pc_shift)*10+idx;
                if(cost<best)
                {
                    best=cost;
                    result.row=(uint8_t)(idx>>3);
                    result.column=(uint8_t)(idx&7);
                    result.shift=(uint8_t)target_shift;
                    result.graph=(uint8_t)target_graph;
                    result.valid=1;
                }
            }
    return result;
}
#endif
