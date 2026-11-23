#include "rfsoc_processing.h"
#include "integrator.h"
#include "io.h"
#include "iq_rot.h"
#include "rfsoc_types.h"

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    const adc_sample_t *in; size_t n; rotated_sample_t *out; size_t n_out;
    int channel; bool fft; size_t fft_len; int status;
} dc_job_t;
typedef struct {
    const rotated_sample_t *in; size_t n; const gate_window_t *windows;
    size_t n_windows; integration_result_t *out; int status;
} int_job_t;
typedef struct { size_t n; double mean, m2; } stats_t;

static double elapsed(const struct timespec *a, const struct timespec *b)
{
    return (double)(b->tv_sec-a->tv_sec)+(double)(b->tv_nsec-a->tv_nsec)*1e-9;
}
static void stats_add(stats_t *s, double x)
{
    const double d=x-s->mean; ++s->n; s->mean+=d/(double)s->n;
    s->m2+=d*(x-s->mean);
}
static double stats_sd(const stats_t *s)
{
    return s->n ? sqrt(s->m2/(double)s->n) : NAN;
}
static void *run_dc(void *arg)
{
    dc_job_t *j=arg;
    j->status=process_to_dc(j->in,j->n,j->out,&j->n_out,
                            RFSOC_SAMPLE_FREQUENCY,j->channel,j->fft,j->fft_len);
    return NULL;
}
static void *run_int(void *arg)
{
    int_job_t *j=arg;
    j->status=integrate_windows_sorted(j->in,j->n,j->windows,j->n_windows,j->out);
    return NULL;
}

static int make_windows(const uint64_t *raw, size_t n_raw,
                        gate_window_t **out, size_t *n_events, size_t *n_windows)
{
    size_t count=0;
    for(size_t i=0;i<n_raw;++i) count += raw[i]!=0;
    if(count<2) return -1;
    gate_event_t *events=malloc(count*sizeof(*events));
    gate_window_t *windows=malloc((count/2)*sizeof(*windows));
    if(!events||!windows){free(events);free(windows);return -2;}
    size_t e=0;
    for(size_t i=0;i<n_raw;++i) if(raw[i]) events[e++]=parse_single_gate_word(raw[i]);
    size_t w=0; bool rising=false; uint64_t start=0;
    for(size_t i=0;i<count;++i){
        if(!events[i].edge){start=events[i].ts;rising=true;}
        else if(rising && events[i].ts>start){
            if(w && start<windows[w-1].end_ts){free(events);free(windows);return -3;}
            windows[w++]=(gate_window_t){start,events[i].ts}; rising=false;
        }
    }
    free(events);
    if(!w){free(windows);return -4;}
    *out=windows; *n_events=count; *n_windows=w; return 0;
}

int process_dma_buffer_ex(const uint64_t *adc, size_t n_adc,
                          const uint64_t *gate, size_t n_gate,
                          size_t packet_words, size_t fft_len, bool fft,
                          rfsoc_processing_result_t *r)
{
    if(!adc||!gate||!r) return -1;
    memset(r,0,sizeof(*r)); r->abi_version=RFSOC_PROCESSING_RESULT_VERSION;
    if(packet_words<5||(packet_words-2)%3||!n_adc||n_adc%packet_words) return -2;
    r->packet_count=n_adc/packet_words;
    const size_t spp=(packet_words-2)/3;
    if(r->packet_count>SIZE_MAX/spp) return -3;
    r->sample_count=r->packet_count*spp;

    int status=-1; adc_sample_t *samples=NULL; rotated_sample_t *dc2=NULL,*dc3=NULL;
    gate_window_t *windows=NULL; integration_result_t *i2=NULL,*i3=NULL;
    pthread_t th[2]; bool active[2]={false,false};
    struct timespec total0,total1,t0,t1; clock_gettime(CLOCK_MONOTONIC,&total0);

    clock_gettime(CLOCK_MONOTONIC,&t0);
    samples=malloc(r->sample_count*sizeof(*samples));
    dc2=malloc(r->sample_count*sizeof(*dc2)); dc3=malloc(r->sample_count*sizeof(*dc3));
    clock_gettime(CLOCK_MONOTONIC,&t1); r->allocation_seconds=elapsed(&t0,&t1);
    if(!samples||!dc2||!dc3){status=-4;goto done;}

    clock_gettime(CLOCK_MONOTONIC,&t0);
    status=adc_packet_parser_v2(adc,n_adc,packet_words,samples);
    clock_gettime(CLOCK_MONOTONIC,&t1); r->parser_seconds=elapsed(&t0,&t1);
    if(status){status=-10+status;goto done;}

    clock_gettime(CLOCK_MONOTONIC,&t0);
    status=make_windows(gate,n_gate,&windows,&r->gate_event_count,&r->window_count);
    if(status){status=-20+status;goto done;}
    i2=calloc(r->window_count,sizeof(*i2)); i3=calloc(r->window_count,sizeof(*i3));
    clock_gettime(CLOCK_MONOTONIC,&t1); r->gate_seconds=elapsed(&t0,&t1);
    if(!i2||!i3){status=-5;goto done;}

    dc_job_t dj[2]={{samples,r->sample_count,dc2,0,2,fft,fft_len,-1},
                    {samples,r->sample_count,dc3,0,3,fft,fft_len,-1}};
    clock_gettime(CLOCK_MONOTONIC,&t0);
    for(size_t k=0;k<2;++k){if(pthread_create(&th[k],NULL,run_dc,&dj[k])){status=-30-(int)k;goto done;}active[k]=true;}
    for(size_t k=0;k<2;++k){if(pthread_join(th[k],NULL)){status=-32-(int)k;goto done;}active[k]=false;}
    clock_gettime(CLOCK_MONOTONIC,&t1); r->dc_seconds=elapsed(&t0,&t1);
    if(dj[0].status||dj[1].status){status=-34;goto done;}
    r->dc_samples_ch2=dj[0].n_out; r->dc_samples_ch3=dj[1].n_out;

    int_job_t ij[2]={{dc2,dj[0].n_out,windows,r->window_count,i2,-1},
                     {dc3,dj[1].n_out,windows,r->window_count,i3,-1}};
    clock_gettime(CLOCK_MONOTONIC,&t0);
    for(size_t k=0;k<2;++k){if(pthread_create(&th[k],NULL,run_int,&ij[k])){status=-40-(int)k;goto done;}active[k]=true;}
    for(size_t k=0;k<2;++k){if(pthread_join(th[k],NULL)){status=-42-(int)k;goto done;}active[k]=false;}
    clock_gettime(CLOCK_MONOTONIC,&t1); r->integration_seconds=elapsed(&t0,&t1);
    if(ij[0].status||ij[1].status){status=-44;goto done;}

    clock_gettime(CLOCK_MONOTONIC,&t0); stats_t s2={0},s3={0},sd={0};
    for(size_t k=0;k+1<r->window_count;k+=2){
        if(!i2[k].n_samples||!i2[k+1].n_samples||!i3[k].n_samples||!i3[k+1].n_samples)continue;
        const double d2=i2[k+1].mean+i2[k].mean,d3=i3[k+1].mean+i3[k].mean;
        if(d2==0.0||d3==0.0)continue;
        const double a=(i2[k+1].mean-i2[k].mean)/d2;
        const double b=(i3[k+1].mean-i3[k].mean)/d3;
        stats_add(&s2,a);stats_add(&s3,b);stats_add(&sd,(a-b)/sqrt(2.0));
    }
    r->valid_pair_count=sd.n; r->rdf2_resolution_ppm=stats_sd(&s2)*1e6;
    r->rdf3_resolution_ppm=stats_sd(&s3)*1e6; r->ddf_resolution_ppm=stats_sd(&sd)*1e6;
    clock_gettime(CLOCK_MONOTONIC,&t1); r->rdf_seconds=elapsed(&t0,&t1);
    status=sd.n?0:-50;

done:
    for(size_t k=0;k<2;++k)if(active[k])pthread_join(th[k],NULL);
    clock_gettime(CLOCK_MONOTONIC,&t0);
    free(i3);free(i2);free(windows);free(dc3);free(dc2);free(samples);
    clock_gettime(CLOCK_MONOTONIC,&t1);r->cleanup_seconds=elapsed(&t0,&t1);
    clock_gettime(CLOCK_MONOTONIC,&total1);r->total_seconds=elapsed(&total0,&total1);
    return status;
}

int process_dma_buffer(const uint64_t *a,size_t na,const uint64_t *g,size_t ng,
                       size_t pw,size_t fl,bool fe)
{
    rfsoc_processing_result_t r;
    return process_dma_buffer_ex(a,na,g,ng,pw,fl,fe,&r);
}
