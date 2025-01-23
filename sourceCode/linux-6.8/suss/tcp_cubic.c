// SPDX-License-Identifier: GPL-2.0-only
/*
 * TCP CUBIC: Binary Increase Congestion control for TCP v2.3
 * Home page:
 *      http://netsrv.csc.ncsu.edu/twiki/bin/view/Main/BIC
 * This is from the implementation of CUBIC TCP in
 * Sangtae Ha, Injong Rhee and Lisong Xu,
 *  "CUBIC: A New TCP-Friendly High-Speed TCP Variant"
 *  in ACM SIGOPS Operating System Review, July 2008.
 * Available from:
 *  http://netsrv.csc.ncsu.edu/export/cubic_a_new_tcp_2008.pdf
 *
 * CUBIC integrates a new slow start algorithm, called HyStart.
 * The details of HyStart are presented in
 *  Sangtae Ha and Injong Rhee,
 *  "Taming the Elephants: New TCP Slow Start", NCSU TechReport 2008.
 * Available from:
 *  http://netsrv.csc.ncsu.edu/export/hystart_techreport_2008.pdf
 *
 * All testing results are available from:
 * http://netsrv.csc.ncsu.edu/wiki/index.php/TCP_Testing
 *
 * Unless CUBIC is enabled and congestion window is large
 * this behaves the same as the original Reno.
 */

#include <linux/mm.h>
#include <linux/btf.h>
#include <linux/btf_ids.h>
#include <linux/module.h>
#include <linux/math64.h>
#include <net/tcp.h>

/* suss start block - E1        */
static int suss = 0;
module_param(suss, int, 0644);
MODULE_PARM_DESC(suss, "0 means suss is inactive");
static int suss_max = 3;
module_param(suss_max, int, 0644);
MODULE_PARM_DESC(suss_max, "max of times the growth factor can be > 2");
static int suss_kmax = 1;
module_param(suss_kmax, int, 0644);
MODULE_PARM_DESC(suss_kmax, "max of the number of RTTs over which the exponential growth can be projected to continue");
/* suss end block	        */

#define BICTCP_BETA_SCALE    1024	/* Scale factor beta calculation
					 * max_cwnd = snd_cwnd * beta
					 */
#define	BICTCP_HZ		10	/* BIC HZ 2^10 = 1024 */

/* Two methods of hybrid slow start */
#define HYSTART_ACK_TRAIN	0x1
#define HYSTART_DELAY		0x2

/* Number of delay samples for detecting the increase of delay */
#define HYSTART_MIN_SAMPLES	8
#define HYSTART_DELAY_MIN	(4000U)	/* 4 ms */
#define HYSTART_DELAY_MAX	(16000U)	/* 16 ms */
#define HYSTART_DELAY_THRESH(x)	clamp(x, HYSTART_DELAY_MIN, HYSTART_DELAY_MAX)

/* suss start block - E2	*/
#define BW_SCALE 24
#define BW_UNIT (1 << BW_SCALE)
#define SUSS_SCALE 10
/* suss end block		*/

static int fast_convergence __read_mostly = 1;
static int beta __read_mostly = 717;	/* = 717/1024 (BICTCP_BETA_SCALE) */
static int initial_ssthresh __read_mostly;
static int bic_scale __read_mostly = 41;
static int tcp_friendliness __read_mostly = 1;

static int hystart __read_mostly = 1;
static int hystart_detect __read_mostly = HYSTART_ACK_TRAIN | HYSTART_DELAY;
static int hystart_low_window __read_mostly = 16;
static int hystart_ack_delta_us __read_mostly = 2000;

static u32 cube_rtt_scale __read_mostly;
static u32 beta_scale __read_mostly;
static u64 cube_factor __read_mostly;

/* Note parameters that are used for precomputing scale factors are read-only */
module_param(fast_convergence, int, 0644);
MODULE_PARM_DESC(fast_convergence, "turn on/off fast convergence");
module_param(beta, int, 0644);
MODULE_PARM_DESC(beta, "beta for multiplicative increase");
module_param(initial_ssthresh, int, 0644);
MODULE_PARM_DESC(initial_ssthresh, "initial value of slow start threshold");
module_param(bic_scale, int, 0444);
MODULE_PARM_DESC(bic_scale, "scale (scaled by 1024) value for bic function (bic_scale/1024)");
module_param(tcp_friendliness, int, 0644);
MODULE_PARM_DESC(tcp_friendliness, "turn on/off tcp friendliness");
module_param(hystart, int, 0644);
MODULE_PARM_DESC(hystart, "turn on/off hybrid slow start algorithm");
module_param(hystart_detect, int, 0644);
MODULE_PARM_DESC(hystart_detect, "hybrid slow start detection mechanisms"
		 " 1: packet-train 2: delay 3: both packet-train and delay");
module_param(hystart_low_window, int, 0644);
MODULE_PARM_DESC(hystart_low_window, "lower bound cwnd for hybrid slow start");
module_param(hystart_ack_delta_us, int, 0644);
MODULE_PARM_DESC(hystart_ack_delta_us, "spacing between ack's indicating train (usecs)");

/* BIC TCP Parameters */
struct bictcp {
	u32	cnt;		/* increase cwnd by 1 after ACKs */
	u32	last_max_cwnd;	/* last maximum snd_cwnd */
	u32	last_cwnd;	/* the last snd_cwnd */
	u32	last_time;	/* time when updated last_cwnd */
	u32	bic_origin_point;/* origin point of bic function */
	u32	bic_K;		/* time to origin point
				   from the beginning of the current epoch */
	u32	delay_min;	/* min delay (usec) */
	u32	epoch_start;	/* beginning of an epoch */
	u32	ack_cnt;	/* number of acks */
	u32	tcp_cwnd;	/* estimated tcp cwnd */
	u16	unused;
	u8	sample_cnt;	/* number of samples to decide curr_rtt */
	u8	found;		/* the exit point is found? */
	u32	round_start;	/* beginning of each round */
	u32	end_seq;	/* end_seq of the round */
	u32	last_ack;	/* last time when the ACK spacing is close */
	u32	curr_rtt;	/* the minimum rtt of current round */
/* suss start block - E3	*/
        u32     suss_head_seq;          /* head of the blue part of the data train              */
        u32     suss_tail_seq;          /* tail of the blue part of the data train              */
        u32     suss_round_start_us;    /* the start time of the current round                  */
        u64     suss_round_no   :5,     /* the current round number                             */
                suss_gf         :1,     /* cwnd is quadrupled when suss_gf is one               */
                suss_is_blue    :2,     /* It is 0 if the received ACK is red                   */
                suss_flag       :1,     /* suss_flag=1 means stop EG when cwnd reaches suss_cap */
                suss_cap        :14,    /* it is used in HyStart to stop EG when suss_flag=1    */
                suss_r_minupdate:5,     /* in which round minRTT was updated                    */
                suss_blue_cnt   :12,    /* number of received blue ACKs in the current round    */
                suss_perv_delta_t_bat:18,/* how long did it take to receive the blue ACK train
                                           in the pervious round                                */
                suss_num_of_jump:3,     /* number of pacing period                              */
                suss_unused     :3;
/* suss end block		*/
};

static inline void bictcp_reset(struct bictcp *ca)
{
	memset(ca, 0, offsetof(struct bictcp, unused));
	ca->found = 0;
}

static inline u32 bictcp_clock_us(const struct sock *sk)
{
	return tcp_sk(sk)->tcp_mstamp;
}

static inline void bictcp_hystart_reset(struct sock *sk)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bictcp *ca = inet_csk_ca(sk);

	ca->round_start = ca->last_ack = bictcp_clock_us(sk);
	ca->end_seq = tp->snd_nxt;
	ca->curr_rtt = ~0U;
	ca->sample_cnt = 0;
}

__bpf_kfunc static void cubictcp_init(struct sock *sk)
{
	struct bictcp *ca = inet_csk_ca(sk);

	bictcp_reset(ca);
/* suss start block - E4	*/
	struct tcp_sock *tp = tcp_sk(sk);
	ca->suss_num_of_jump = 0;
	ca->suss_gf   = 1;
	ca->suss_flag = 0;
	ca->suss_cap  = 0;
	ca->suss_round_no = 1;
	ca->suss_head_seq = tp->snd_nxt;
	ca->suss_tail_seq = tp->snd_nxt + (10 * tp->mss_cache) - 1;
	if (suss && (sk->sk_pacing_status == SK_PACING_NONE) && (inet_sk(sk)->inet_sport==20480 || inet_sk(sk)->inet_dport==20480))
	    tp->suss_state = 1;
	else
	    tp->suss_state = 10;
/* suss end block       */
	if (hystart)
		bictcp_hystart_reset(sk);

	if (!hystart && initial_ssthresh)
		tcp_sk(sk)->snd_ssthresh = initial_ssthresh;
}

__bpf_kfunc static void cubictcp_cwnd_event(struct sock *sk, enum tcp_ca_event event)
{
	if (event == CA_EVENT_TX_START) {
		struct bictcp *ca = inet_csk_ca(sk);
		u32 now = tcp_jiffies32;
		s32 delta;

		delta = now - tcp_sk(sk)->lsndtime;

		/* We were application limited (idle) for a while.
		 * Shift epoch_start to keep cwnd growth to cubic curve.
		 */
		if (ca->epoch_start && delta > 0) {
			ca->epoch_start += delta;
			if (after(ca->epoch_start, now))
				ca->epoch_start = now;
		}
		return;
	}
}

/* calculate the cubic root of x using a table lookup followed by one
 * Newton-Raphson iteration.
 * Avg err ~= 0.195%
 */
static u32 cubic_root(u64 a)
{
	u32 x, b, shift;
	/*
	 * cbrt(x) MSB values for x MSB values in [0..63].
	 * Precomputed then refined by hand - Willy Tarreau
	 *
	 * For x in [0..63],
	 *   v = cbrt(x << 18) - 1
	 *   cbrt(x) = (v[x] + 10) >> 6
	 */
	static const u8 v[] = {
		/* 0x00 */    0,   54,   54,   54,  118,  118,  118,  118,
		/* 0x08 */  123,  129,  134,  138,  143,  147,  151,  156,
		/* 0x10 */  157,  161,  164,  168,  170,  173,  176,  179,
		/* 0x18 */  181,  185,  187,  190,  192,  194,  197,  199,
		/* 0x20 */  200,  202,  204,  206,  209,  211,  213,  215,
		/* 0x28 */  217,  219,  221,  222,  224,  225,  227,  229,
		/* 0x30 */  231,  232,  234,  236,  237,  239,  240,  242,
		/* 0x38 */  244,  245,  246,  248,  250,  251,  252,  254,
	};

	b = fls64(a);
	if (b < 7) {
		/* a in [0..63] */
		return ((u32)v[(u32)a] + 35) >> 6;
	}

	b = ((b * 84) >> 8) - 1;
	shift = (a >> (b * 3));

	x = ((u32)(((u32)v[shift] + 10) << b)) >> 6;

	/*
	 * Newton-Raphson iteration
	 *                         2
	 * x    = ( 2 * x  +  a / x  ) / 3
	 *  k+1          k         k
	 */
	x = (2 * x + (u32)div64_u64(a, (u64)x * (u64)(x - 1)));
	x = ((x * 341) >> 10);
	return x;
}

/*
 * Compute congestion window to use.
 */
static inline void bictcp_update(struct bictcp *ca, u32 cwnd, u32 acked)
{
	u32 delta, bic_target, max_cnt;
	u64 offs, t;

	ca->ack_cnt += acked;	/* count the number of ACKed packets */

	if (ca->last_cwnd == cwnd &&
	    (s32)(tcp_jiffies32 - ca->last_time) <= HZ / 32)
		return;

	/* The CUBIC function can update ca->cnt at most once per jiffy.
	 * On all cwnd reduction events, ca->epoch_start is set to 0,
	 * which will force a recalculation of ca->cnt.
	 */
	if (ca->epoch_start && tcp_jiffies32 == ca->last_time)
		goto tcp_friendliness;

	ca->last_cwnd = cwnd;
	ca->last_time = tcp_jiffies32;

	if (ca->epoch_start == 0) {
		ca->epoch_start = tcp_jiffies32;	/* record beginning */
		ca->ack_cnt = acked;			/* start counting */
		ca->tcp_cwnd = cwnd;			/* syn with cubic */

		if (ca->last_max_cwnd <= cwnd) {
			ca->bic_K = 0;
			ca->bic_origin_point = cwnd;
		} else {
			/* Compute new K based on
			 * (wmax-cwnd) * (srtt>>3 / HZ) / c * 2^(3*bictcp_HZ)
			 */
			ca->bic_K = cubic_root(cube_factor
					       * (ca->last_max_cwnd - cwnd));
			ca->bic_origin_point = ca->last_max_cwnd;
		}
	}

	/* cubic function - calc*/
	/* calculate c * time^3 / rtt,
	 *  while considering overflow in calculation of time^3
	 * (so time^3 is done by using 64 bit)
	 * and without the support of division of 64bit numbers
	 * (so all divisions are done by using 32 bit)
	 *  also NOTE the unit of those veriables
	 *	  time  = (t - K) / 2^bictcp_HZ
	 *	  c = bic_scale >> 10
	 * rtt  = (srtt >> 3) / HZ
	 * !!! The following code does not have overflow problems,
	 * if the cwnd < 1 million packets !!!
	 */

	t = (s32)(tcp_jiffies32 - ca->epoch_start);
	t += usecs_to_jiffies(ca->delay_min);
	/* change the unit from HZ to bictcp_HZ */
	t <<= BICTCP_HZ;
	do_div(t, HZ);

	if (t < ca->bic_K)		/* t - K */
		offs = ca->bic_K - t;
	else
		offs = t - ca->bic_K;

	/* c/rtt * (t-K)^3 */
	delta = (cube_rtt_scale * offs * offs * offs) >> (10+3*BICTCP_HZ);
	if (t < ca->bic_K)                            /* below origin*/
		bic_target = ca->bic_origin_point - delta;
	else                                          /* above origin*/
		bic_target = ca->bic_origin_point + delta;

	/* cubic function - calc bictcp_cnt*/
	if (bic_target > cwnd) {
		ca->cnt = cwnd / (bic_target - cwnd);
	} else {
		ca->cnt = 100 * cwnd;              /* very small increment*/
	}

	/*
	 * The initial growth of cubic function may be too conservative
	 * when the available bandwidth is still unknown.
	 */
	if (ca->last_max_cwnd == 0 && ca->cnt > 20)
		ca->cnt = 20;	/* increase cwnd 5% per RTT */

tcp_friendliness:
	/* TCP Friendly */
	if (tcp_friendliness) {
		u32 scale = beta_scale;

		delta = (cwnd * scale) >> 3;
		while (ca->ack_cnt > delta) {		/* update tcp cwnd */
			ca->ack_cnt -= delta;
			ca->tcp_cwnd++;
		}

		if (ca->tcp_cwnd > cwnd) {	/* if bic is slower than tcp */
			delta = ca->tcp_cwnd - cwnd;
			max_cnt = cwnd / delta;
			if (ca->cnt > max_cnt)
				ca->cnt = max_cnt;
		}
	}

	/* The maximum rate of cwnd increase CUBIC allows is 1 packet per
	 * 2 packets ACKed, meaning cwnd grows at 1.5x per RTT.
	 */
	ca->cnt = max(ca->cnt, 2U);
}

/* suss start block - E7	*/
static u8 suss_speedup(struct sock *sk, u32 delta_t_bat)
{
    struct bictcp *ca = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);

    u8 value = 0;

    if (ca->suss_round_no == 2) {
	if (ca->delay_min > 10000 && delta_t_bat < (ca->delay_min >> 2)) {
	    value = 1;
	} else {
	    tp->suss_state = 10; // Disable SUSS
	}
    } else {
	u32 delta_t = delta_t_bat << (ca->suss_round_no - 2);
	u32 perv_delta_t = ca->suss_perv_delta_t_bat << (ca->suss_round_no - 3);
	u64 mu = div64_u64((u64)delta_t << SUSS_SCALE, (perv_delta_t << 1));
	u64 temp = (mu * delta_t) >> (SUSS_SCALE - 1);

	/* Condition 1 */
	if ((temp <= ca->delay_min) && (ca->suss_round_no < (suss_max + 2))) {
	    value = 1;
	} else {
	    value = 0;
	}

	/* Condition 2 */
	if (value == 1) {
	    u32 k = ca->suss_round_no - ca->suss_r_minupdate;
	    u64 temp1 = (u64)(k + 1) * ca->curr_rtt;
	    u64 temp2 = (u64)(ca->delay_min * (k + 1)) + ((ca->delay_min * k) >> 3);

	    if (temp1 > temp2) {
		value = 0;
	    }
	}

	printk(KERN_INFO "SUSSmsg id=%u Growth factor measured. t=%u Sport=%u G=%u",
	 tp->suss_msg_id, bictcp_clock_us(sk), inet_sk(sk)->inet_sport, 2 << value);
    }

    return value;
}
/* suss end block		*/

/* suss start block - E11	*/
__bpf_kfunc static void suss_measurements(struct sock *sk, u32 ack, u32 acked)
{
    struct tcp_sock *tp = tcp_sk(sk);
    struct bictcp *ca = inet_csk_ca(sk);
    u32 now = bictcp_clock_us(sk);

    /* Is it the first red ACK */
    if (ca->suss_is_blue == 2)
	ca->suss_is_blue = 0;

    /* If it is the head of an ACK train then
    1) increase round counter    2) switch to ACK clocking mode  */
    if (ack > ca->suss_head_seq) {
	ca->suss_is_blue = 1;
	ca->suss_blue_cnt = 0;
	ca->suss_round_no ++;
	ca->suss_round_start_us = now;
	ca->suss_head_seq = tp->snd_nxt;
	printk(KERN_INFO "SUSSmsg id=%u New round %u is started. t=%u Sport=%u c=%u i=%u",
	 tp->suss_msg_id, ca->suss_round_no, now, inet_sk(sk)->inet_sport, tp->snd_cwnd, tcp_packets_in_flight(tp));

	cmpxchg(&sk->sk_pacing_status, SK_PACING_NEEDED, SK_PACING_NONE);//swtich to ACK clocking mode
	tp->suss_state = 1;
    }

    /* If this is the tail of the blue part of an ACK train: measure delta_t_bat and quadruple cwnd if all conditions are satisfied. */
    if (ack > ca->suss_tail_seq) {
	u64 rate, guard;
	u32 temp, elapsed, blue_pkt, blue_ack, red_pkt, red_ack, delta_t_bat, pacing_duration;

	elapsed = (now - ca->suss_round_start_us);
	ca->suss_is_blue = 2;
	blue_pkt = TCP_INIT_CWND << (ca->suss_round_no - 1);
	blue_ack = blue_pkt >> 1;
	ca->suss_tail_seq = ca->suss_head_seq + (blue_pkt * tp->mss_cache) - 1;

	temp = ca->suss_blue_cnt << 1;
	delta_t_bat= div64_long((u64) (blue_pkt * elapsed), temp);
	printk(KERN_INFO "SUSSmsg id=%u Blue ACK train in round %u is received in %u us. t=%u Sport=%u dtB=%u c=%u i=%u",
	 tp->suss_msg_id, ca->suss_round_no, elapsed, now, inet_sk(sk)->inet_sport, delta_t_bat, tp->snd_cwnd, tcp_packets_in_flight(tp));

	if (ca->suss_gf == 1) {
	    ca->suss_gf = suss_speedup(sk, delta_t_bat);
	}

	if (ca->suss_gf == 1) {
	    ca->suss_perv_delta_t_bat = delta_t_bat;
	    red_pkt = (blue_pkt << (ca->suss_round_no - 1)) - blue_pkt;
	    red_ack = (blue_ack << (ca->suss_round_no - 2)) - blue_ack;
	    tp->suss_limit = (tp->snd_cwnd + acked) + red_pkt - red_ack;
	    pacing_duration = ca->delay_min - (ca->delay_min >> (ca->suss_round_no - 1));
	    rate = div64_long((u64) (red_pkt * tp->mss_cache) * BW_UNIT, pacing_duration);
	    rate *= USEC_PER_SEC;
	    rate = rate >> BW_SCALE;
	    tp->suss_rate = rate;

	    temp = ca->delay_min - pacing_duration;
	    if (temp > delta_t_bat) {
		tp->suss_state = 2;
		ca->suss_num_of_jump += 1;
		guard = ((temp - delta_t_bat) >> 1) * NSEC_PER_USEC;
		tp->suss_pacing_start_ns = tp->tcp_clock_cache + guard;
		printk(KERN_INFO "SUSSmsg id=%u Total amount of %u packets is paced in %u microSec with rate %llu Bps starting from time %llu ns. t=%u Sport=%u limit=%u delta_t_bat=%u guard=%llu",
		 tp->suss_msg_id, red_pkt, pacing_duration, rate, tp->suss_pacing_start_ns, now, inet_sk(sk)->inet_sport, tp->suss_limit, delta_t_bat, guard);
	    } else {
		tp->suss_state = 1;
	    }
	}
    }

    if (ca->suss_is_blue != 0) {
	ca->suss_blue_cnt += acked;
    }

    if (ca->suss_is_blue == 0 && tp->suss_state == 2) {
	tp->snd_cwnd -= acked;
    }
}
/* suss end block	*/

__bpf_kfunc static void cubictcp_cong_avoid(struct sock *sk, u32 ack, u32 acked)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bictcp *ca = inet_csk_ca(sk);

/* suss start block - E6	*/
	u32 una = tp->snd_una - tp->snd_isn;
	u8  temp = 0;

	if ((tp->suss_state > 2) && (tp->suss_state < 10) && (sk->sk_pacing_status == SK_PACING_NEEDED))
		cmpxchg(&sk->sk_pacing_status, SK_PACING_NEEDED, SK_PACING_NONE); //swtich to clocking mode

	if (tp->suss_state < 3 && ca->suss_flag == 0)
		suss_measurements(sk, ack, acked);


	if ((tp->suss_state < 3) && (ca->suss_round_no < (suss_max + ca->suss_num_of_jump)))
		temp = (ca->suss_round_no + ca->suss_num_of_jump - 1);

	tp->snd_wnd = max(tp->snd_wnd, ((10 * tp->mss_cache) << temp));

	printk(KERN_INFO "SUSSmsg@ id=%u t=%llu Sport=%u c=%u i=%u a=%u "
	"RTT=%u moRTT=%u minRTT=%u d=%u l=%u "
	"tRnd=%u s=%u Bcnt=%u Rnd=%u ",
	tp->suss_msg_id, tp->tcp_mstamp, inet_sk(sk)->inet_sport, tp->snd_cwnd, tcp_packets_in_flight(tp), acked,
	(tp->srtt_us >> 3), ca->curr_rtt, ca->delay_min, una, tp->lost,
	ca->round_start, tp->suss_state, ca->suss_blue_cnt, ca->suss_round_no);
/* suss end block		*/

	if (!tcp_is_cwnd_limited(sk))
		return;

	if (tcp_in_slow_start(tp)) {
		acked = tcp_slow_start(tp, acked);
		if (!acked)
			return;
	}
	bictcp_update(ca, tcp_snd_cwnd(tp), acked);
	tcp_cong_avoid_ai(tp, ca->cnt, acked);
}

__bpf_kfunc static u32 cubictcp_recalc_ssthresh(struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	struct bictcp *ca = inet_csk_ca(sk);

	ca->epoch_start = 0;	/* end of epoch */

	/* Wmax and fast convergence */
	if (tcp_snd_cwnd(tp) < ca->last_max_cwnd && fast_convergence)
		ca->last_max_cwnd = (tcp_snd_cwnd(tp) * (BICTCP_BETA_SCALE + beta))
			/ (2 * BICTCP_BETA_SCALE);
	else
		ca->last_max_cwnd = tcp_snd_cwnd(tp);

	return max((tcp_snd_cwnd(tp) * beta) / BICTCP_BETA_SCALE, 2U);
}

__bpf_kfunc static void cubictcp_state(struct sock *sk, u8 new_state)
{
	if (new_state == TCP_CA_Loss) {
		bictcp_reset(inet_csk_ca(sk));
		bictcp_hystart_reset(sk);
	}
}

/* Account for TSO/GRO delays.
 * Otherwise short RTT flows could get too small ssthresh, since during
 * slow start we begin with small TSO packets and ca->delay_min would
 * not account for long aggregation delay when TSO packets get bigger.
 * Ideally even with a very small RTT we would like to have at least one
 * TSO packet being sent and received by GRO, and another one in qdisc layer.
 * We apply another 100% factor because @rate is doubled at this point.
 * We cap the cushion to 1ms.
 */
static u32 hystart_ack_delay(const struct sock *sk)
{
	unsigned long rate;

	rate = READ_ONCE(sk->sk_pacing_rate);
	if (!rate)
		return 0;
	return min_t(u64, USEC_PER_MSEC,
		     div64_ul((u64)sk->sk_gso_max_size * 4 * USEC_PER_SEC, rate));
}

/* suss start block - E12	*/
static void suss_cap(struct sock *sk)
{
    struct tcp_sock *tp = tcp_sk(sk);
    struct bictcp *ca = inet_csk_ca(sk);
    u32 threshold;
    u32 now = bictcp_clock_us(sk);

    u32 temp;
    if (ca->suss_flag == 1 && tp->snd_cwnd > ca->suss_cap) {
	ca->found = 1;
	printk(KERN_INFO "SUSSmsg id=%u Stop exponential growth (type=3): t=%u Sport=%u cap=%u ssthresh=%u c=%u i=%u",
	 tp->suss_msg_id, now, inet_sk(sk)->inet_sport, ca->suss_cap, tp->snd_ssthresh, tp->snd_cwnd, tcp_packets_in_flight(tp));
	tp->suss_state = 3;
	if (ca->suss_num_of_jump > 1) {
	    tp->snd_cwnd = tcp_packets_in_flight(tp);
	}

	tp->snd_ssthresh = tp->snd_cwnd;
	return;
    }

    if (ca->suss_flag == 0 && tp->suss_state < 3 && ca->suss_is_blue != 0) {
	if ((s32)(now - ca->last_ack) <= hystart_ack_delta_us) {
	    ca->last_ack = now;
	    threshold = ca->delay_min + hystart_ack_delay(sk);
	    threshold >>= 1;
	    temp = (now - ca->round_start) << ca->suss_num_of_jump;
	    if (temp > threshold) {
		ca->suss_flag = 1;
		ca->suss_cap = tp->snd_cwnd + (ca->suss_blue_cnt * ((1 << ca->suss_num_of_jump) - 1));
		printk(KERN_INFO "SUSSmsg id=%u Cap is set: t=%u Sport=%u cap=%u ssthresh=%u c=%u i=%u",
		 tp->suss_msg_id, now, inet_sk(sk)->inet_sport, ca->suss_cap, tp->snd_ssthresh, tp->snd_cwnd, tcp_packets_in_flight(tp));
	    }
	}
    }
}
/* suss end block		*/

static void hystart_update(struct sock *sk, u32 delay)
{
	struct tcp_sock *tp = tcp_sk(sk);
	struct bictcp *ca = inet_csk_ca(sk);
	u32 threshold;

	if (after(tp->snd_una, ca->end_seq))
		bictcp_hystart_reset(sk);

	/* hystart triggers when cwnd is larger than some threshold */
	if (tcp_snd_cwnd(tp) < hystart_low_window)
		return;

	if (hystart_detect & HYSTART_ACK_TRAIN) {
		u32 now = bictcp_clock_us(sk);
/* suss start block - E8	*/
		suss_cap(sk);
        	if (tp->suss_state == 10)
/* suss end block       	*/

		/* first detection parameter - ack-train detection */
		if ((s32)(now - ca->last_ack) <= hystart_ack_delta_us) {
			ca->last_ack = now;

			threshold = ca->delay_min + hystart_ack_delay(sk);

			/* Hystart ack train triggers if we get ack past
			 * ca->delay_min/2.
			 * Pacing might have delayed packets up to RTT/2
			 * during slow start.
			 */
			if (sk->sk_pacing_status == SK_PACING_NONE)
				threshold >>= 1;

			if ((s32)(now - ca->round_start) > threshold) {
				ca->found = 1;
			/* suss start block - E10	*/
				printk(KERN_INFO "SUSSmsg id=%u Stop exponential growth (type=1): t=%u Sport=%u roundStart=%u ssthresh=%u c=%u i=%u",
				 tp->suss_msg_id, now, inet_sk(sk)->inet_sport, ca->round_start, tp->snd_ssthresh, tp->snd_cwnd, tcp_packets_in_flight(tp));
			/* suss end block		*/
				pr_debug("hystart_ack_train (%u > %u) delay_min %u (+ ack_delay %u) cwnd %u\n",
					 now - ca->round_start, threshold,
					 ca->delay_min, hystart_ack_delay(sk), tcp_snd_cwnd(tp));
				NET_INC_STATS(sock_net(sk),
					      LINUX_MIB_TCPHYSTARTTRAINDETECT);
				NET_ADD_STATS(sock_net(sk),
					      LINUX_MIB_TCPHYSTARTTRAINCWND,
					      tcp_snd_cwnd(tp));
				tp->snd_ssthresh = tcp_snd_cwnd(tp);
			}
		}
	}

	if (hystart_detect & HYSTART_DELAY) {
		/* obtain the minimum delay of more than sampling packets */
		if (ca->curr_rtt > delay)
			ca->curr_rtt = delay;
		if (ca->sample_cnt < HYSTART_MIN_SAMPLES) {
			ca->sample_cnt++;
		} else {
			if (ca->curr_rtt > ca->delay_min +
			    HYSTART_DELAY_THRESH(ca->delay_min >> 3)) {
				ca->found = 1;
				/* suss start block - E9	*/
				printk(KERN_INFO "SUSSmsg id=%u Stop exponential growth (type=2): t=%u Sport=%u roundStart=%u ssthresh=%u c=%u i=%u",
				 tp->suss_msg_id, bictcp_clock_us(sk), inet_sk(sk)->inet_sport, ca->round_start, tp->snd_ssthresh, tp->snd_cwnd, tcp_packets_in_flight(tp));
				if (tp->suss_state < 9)	{
					tp->suss_state = 4;
					if (ca->suss_num_of_jump > 1)
					tp->snd_cwnd = tcp_packets_in_flight(tp);
				}
				/* suss end block		*/
				NET_INC_STATS(sock_net(sk),
					      LINUX_MIB_TCPHYSTARTDELAYDETECT);
				NET_ADD_STATS(sock_net(sk),
					      LINUX_MIB_TCPHYSTARTDELAYCWND,
					      tcp_snd_cwnd(tp));
				tp->snd_ssthresh = tcp_snd_cwnd(tp);
			}
		}
	}
}

__bpf_kfunc static void cubictcp_acked(struct sock *sk, const struct ack_sample *sample)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	struct bictcp *ca = inet_csk_ca(sk);
	u32 delay;

	/* Some calls are for duplicates without timetamps */
	if (sample->rtt_us < 0)
		return;

	/* Discard delay samples right after fast recovery */
	if (ca->epoch_start && (s32)(tcp_jiffies32 - ca->epoch_start) < HZ)
		return;

	delay = sample->rtt_us;
	if (delay == 0)
		delay = 1;

	/* first time call or link delay decreases */
	if (ca->delay_min == 0 || ca->delay_min > delay)
	{	/* suss line - E5	*/
		ca->delay_min = delay;
		ca->suss_r_minupdate = ca->suss_round_no;	/* suss line - E5	*/
	}	/* suss line - E5	*/

	if (!ca->found && tcp_in_slow_start(tp) && hystart)
		hystart_update(sk, delay);
}

static struct tcp_congestion_ops cubictcp __read_mostly = {
	.init		= cubictcp_init,
	.ssthresh	= cubictcp_recalc_ssthresh,
	.cong_avoid	= cubictcp_cong_avoid,
	.set_state	= cubictcp_state,
	.undo_cwnd	= tcp_reno_undo_cwnd,
	.cwnd_event	= cubictcp_cwnd_event,
	.pkts_acked     = cubictcp_acked,
	.owner		= THIS_MODULE,
	.name		= "cubic",
};

BTF_SET8_START(tcp_cubic_check_kfunc_ids)
#ifdef CONFIG_X86
#ifdef CONFIG_DYNAMIC_FTRACE
BTF_ID_FLAGS(func, cubictcp_init)
BTF_ID_FLAGS(func, cubictcp_recalc_ssthresh)
BTF_ID_FLAGS(func, cubictcp_cong_avoid)
BTF_ID_FLAGS(func, cubictcp_state)
BTF_ID_FLAGS(func, cubictcp_cwnd_event)
BTF_ID_FLAGS(func, cubictcp_acked)
#endif
#endif
BTF_SET8_END(tcp_cubic_check_kfunc_ids)

static const struct btf_kfunc_id_set tcp_cubic_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &tcp_cubic_check_kfunc_ids,
};

static int __init cubictcp_register(void)
{
	int ret;

	BUILD_BUG_ON(sizeof(struct bictcp) > ICSK_CA_PRIV_SIZE);

	/* Precompute a bunch of the scaling factors that are used per-packet
	 * based on SRTT of 100ms
	 */

	beta_scale = 8*(BICTCP_BETA_SCALE+beta) / 3
		/ (BICTCP_BETA_SCALE - beta);

	cube_rtt_scale = (bic_scale * 10);	/* 1024*c/rtt */

	/* calculate the "K" for (wmax-cwnd) = c/rtt * K^3
	 *  so K = cubic_root( (wmax-cwnd)*rtt/c )
	 * the unit of K is bictcp_HZ=2^10, not HZ
	 *
	 *  c = bic_scale >> 10
	 *  rtt = 100ms
	 *
	 * the following code has been designed and tested for
	 * cwnd < 1 million packets
	 * RTT < 100 seconds
	 * HZ < 1,000,00  (corresponding to 10 nano-second)
	 */

	/* 1/c * 2^2*bictcp_HZ * srtt */
	cube_factor = 1ull << (10+3*BICTCP_HZ); /* 2^40 */

	/* divide by bic_scale and by constant Srtt (100ms) */
	do_div(cube_factor, bic_scale * 10);

	ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS, &tcp_cubic_kfunc_set);
	if (ret < 0)
		return ret;
	return tcp_register_congestion_control(&cubictcp);
}

static void __exit cubictcp_unregister(void)
{
	tcp_unregister_congestion_control(&cubictcp);
}

module_init(cubictcp_register);
module_exit(cubictcp_unregister);

MODULE_AUTHOR("Sangtae Ha, Stephen Hemminger");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CUBIC TCP");
MODULE_VERSION("2.3");
