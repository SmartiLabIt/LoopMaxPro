// The extended table of the makers of ExpNet, in the program of the boards
// with room (LMP_MODULE_MEM 2 and 3), given to the page by the command makers. Written by
// tools/oui.py from the IEEE registry: not by hand. For each maker its name, the type it makes
// most ("" none) and its prefixes not in the table of the page, sorted and written as the
// distances between them, 5 bits a character; a large maker in pieces. 8049 prefixes.
#pragma once

#include <stdint.h>

namespace {

struct Maker {
  const char* name;
  const char* type;
  const char* packed;
};

constexpr Maker kMakers[] = {
    {"Oppo", "phone",
     "Wto1bfs6q-j1stx6qna1iqj7clAyfs6fwMpfr7yv5giFarAknY8oo1Yug6aZGno1evBr_I_l6XtX7_i7WYX7mqRv3WY1"
     "bkuFyBybJxqKlnd6cYDvt7te4ag1exO--t6Wku7nmHpw6brMsgp6bvGpzVkqb7elAznp6urOlzHaYKdtg6jx2wpGt_2p"
     "g4wwBvXr6ZDqrDhr7sv8y-LWsc6da9rt4ZpIhuX7rh4ldHgIbbt7_rDcaJdz3nzl6aXKuge7zf6e_i1xbiFouT-wt6hu"
     "UfqX8Xcg7sg2viHl_2am3Xyn6_kDnt_7jg9bttFxWb7zmLbo3XcRtkk6-e9nWu8ybm7WtGxZEtvd7_hd7xpKfb5woDqs"
     "Ico1Wrb6mfW1p6-x-6wKcwAX-Y1bv9ytg6qhRa-3zcBxYW7eiBwNeY1fnIrkW7azOjy2ly4dv9Y_h6hyDtqGlv2hj9kW"
     "x6b_Z1hJrfy6llAXXX1hfn6eDuvRWtJnuq6hbe1-Fyr6r-y7pwk6ixQk_X7Yh-7WmDqy5dvKZ_u6elG-bz7gY6fyEqx2"
     "ZjZ7mqo7nr9fd3vt1w-IveCfl7boX7ttKcon6YjBin8cie1c_y6mo1mZ3uXt8wpj6qWk1i2Yvl6qje1"},
    {"vivo", "phone",
     "Wcd1utW7w_W8c_MtWDgsHwof6pxFnYt8glExuv7oef6rAyqt1uc1ujg6qeQi4f_Flx7rtr6jo2-k8kq3_Z2ouBfeGju4"
     "b_g6fxNyqh7v-Sae6ou5hg9Xlp6adLfqOjlm6woKvy6fir6v-3X_q1wvp6YCrcDlXc8o_6b4vHr4qFjr3cfIZkn6cr4c"
     "my7jbLzZNk_Y7aSgrZ8up9jzf8spf6yO_Z2ntWGwfz8ymc7iZ6g_f1bnz6jYl7lk1YY7YqW1ln3pz1lXt6kp4YbCyr5Z"
     "ZAvh8qns6diHoYJj-x6xWa9phn7saDjhbFZn5keZ1tlq6-k7uyTs8W-w6rlOpw5fv3dWm6ng7djCqWp7zwJrkIfkk7_v"
     "8hpGgz4fuf6mlx8eaz6mj3mXNedq7ewy7jYt7zoNxwBhjw6efp1Xxp6-y2ky3-gMhvh7bwd8veHuhrFWkb8ak1mvn7xc"
     "Dlu8aW4_c8-he6wYFoaMmzz6_n4jc1goMwyX7hmo1qWv6qi5wPmzEYyMyxhEX-MfnOoXl6rmZ8xpElb3euHqW1-e-6xr"
     "i1npi6vOcrZ1hr4"},
    {"Motorola", "phone",
     "XmOrrLenGtbs7iyw6qvQok8-ff7hY3rkJee8lql6jps7we5Zn5qX2jnf8t-z7ol3Wlq7ZaTre3WpZ7ntRYy7Wh1rXk6m"
     "tzFdgGhcEqwj8vjn7WsOeZ2ZZZFxy3vtSXie6iZPh-uGdph6tUdc6tb5ngbHb-z6fWp7afe1ltq6uhz8iw2blCWvq7z_"
     "-Eldd1wq3fXlFYg9Wd1wzi7Zv4o_e7--Hpie8evEWjc7ise1nzz6tq2dlFub2jjyEnhh9Zgx6yYt7xv2btG_if8_XjFW"
     "tBpfI_hyEplIqp6mj2iv-6swUiuPgxw6ye8jcl7YyEoy6ZYk7tpHmMqh9fm4vaj6p_KbmOtWm6puSzXv7uPzf1biPqpX"
     "8gXy7qij7at8bu2lxW7fjQYbW8b-z6jzVXdPtnw7tnc8ZQkmg6pai1qb4at3vIfXo7to6"},
    {"Fairphone", "phone",
     "_zpf8"},
    {"Tecno", "phone",
     "xjW8mra1efPjaZ7zpBWeE-Xb6vvZGWnVxq-7qm_7bx8vv-6nfbHer5ay-6ubY8czc1r_dFwyX7ZbFp_Sran7wv2sg3Xx"
     "p7wZScYlFe-5ln9ivs6kzW9_Xq6qb9gjf1hXj7_YlGzyzEbow7cZ8YkKqda7Wda1oa9lXh6kXA-gn1t_Y7Z_w7vDoulG"
     "jqdFmrHtlBtfgFaoGis9iY_7ybXNYYn7rl1Wa1Zy8-fwGXypEg-X1Y_c8-m6zfd7_wFlu6Zm1XwZ7Zqv7reu7kp3zt_7"
     "rYLhxn7oXLngq7dr8kZY8_ru8_mo7lwW7__g1uso6YoY1rbfFfll7_q3"},
    {"Garmin", "phone",
     "lg1rc-ObZq7pZUlWp6sWVqswe2ppmk2lsv8Wnpu2jvjUclcf1fcYW1nd_s1rdqW1"},
    {"Fitbit", "phone",
     "xcWm1WzfX4qfp7lgmu6itcl2"},
    {"Liteon", "pc",
     "oa5jk3knq1xvxUqvz7nsMnic8_nb7xfi7bkKpwy7fsLvqIrlv7pex7vYm6umGWrVrWmNfS-w2e-p8ef_6jcv8-iYF-ci"
     "8ZYz6_ra9dssFxgj8i-f7ael7mh4gwh1pYAWus6gia1qCai1cuoMgWsd1ytZ1gnKshk6kbZ8mxX1ZXfGjosFpqAaif6m"
     "ecGZ_P--PjsnEsloHoji7zZY7ol6nYZ1YXbGwvbEq_d9ww_6wvDnkPjj2XlcNyfl7zrU"},
    {"Dell", "pc",
     "xo1vGdRxBfEaHy7p9m5ZDcMk9d7ZCc7cAw3dLh1-3rCw9y3rAew9lsOph3iGhbf7XnMcYCZre7h_b7Xwk8g_5_t6TXYW"
     "7uoX1mY5syI-bX6btLYa3XsGtoCfy7fbn6vlCola7uvGfcGz-o7fr6yLhjHojo6_Yy1nh-7Zkv7fzn6YeCjpAzxSngh6"
     "nvw1_qf6un1jci8ieNvnx7yff7oxGrfv7qzJ-f4uJzbm7akh7bvLlX8sto7mzc7p-q1urd6xqGwff7wtp8r_s7pf4yg-"
     "8_nm7po3e3WWOfdn6vl6WqPWWxEdta8ceCwz7-x4hZu7cdb7auGXoe1iIZfe7kz8-vt6XZi8x-JjrLskmEvoz7sgBpmR"
     "o4ufW7acNqcW7axT_s8pqc7oWBvc2eem7fWd1fb2wbm6ouMht9hk7l7ofv6ggz7re6qY2Zza8mfSot2pao6mZBwxg1gd"
     "q6hrJWuBvf_6kNvk8qq9jl1lgBYXm7sd8by2ba42wWu7ziCnxe8dYv7jOs_v6zi4oaPgqDxgz6ceJhRpj6lts6foo1qt"
     "g6plBWwX1kih6e-Y1m-Nkwm7yr2cr3mc4Yhy6"},
    {"Lenovo", "pc",
     "-t4fmHkaxW1nfy6YfQhrrNdcpm2gh-EycdX1yhXNnta8-ma7slaPWfEhgeFawZX1Zob7cad7xtt7dwUrygFvgfv1xmg6"
     "xocGzhi8xi7xwkFZgtO"},
    {"Clevo", "pc",
     "rda1chwW8rfcd5"},
    {"Nintendo", "tv",
     "_j2ta3rAoBW7i7n4m3q6-7v3q3f4p4YCs3l4-2j1w2x4o2c1l5j5v5m5jct7brfe1huc8fon7hZ4tuiHmhm6hf-8nkf7"
     "lxVfem7vqLzsu7iwc7piZ1zh8mhlEmZTaWJ_pl6Zuy7p-YG-dW1bh3Zwp6ufHfbo7ufKdko8ZozMkd7nyf1X_a7XhTxs"
     "f6ddOqe8gdc8xwb8Wix7WpbFZvTWwz6Zza8YjDlti7jiTixAbr1xf_7tqqExw8wbPd8ydkGiixMsz3cs4Yy8zkn7xg5r"
     "e5gxBgrr7sveF-fIklo7lsGbhNajs6kwc9iv-6q3qd_7jXPdxl6oss1kZ2st1vWsE_lKpgEjlw7"},
    {"Roku", "tv",
     "hg3sX-FnzqGhqbX1oq-Uvzy7jdgt1x_n8u-mOZdrt1YwWHyzy7vrw6oZgX1ezmN-dX7_yS-e-7awH--pEyuO_oj8tquM"
     "rvi8iyt7mjy8pw8bup6gfY8teuW1trbO"},
    {"Sony", "tv",
     "gAli2kPtEmOx5tJu4xAmAo8Bv6hAa4o4h4t4e8wDw7j5i5j6n1u7cgn1-my6xte7YxpGbaiFqt-8rgs7rfp7-jd8_YsF"
     "-g8i_Ef-CoeKrp_EjiY7Yi1ZiCjry7ce5myTrXl7qZOmdpEiay7X_a8iW7aj4_um7sAW-BYex8hfmN-zb7ij5ZauOhjb"
     "Ghtv6jmZ1heE-lW8wnc7pdt8teZ8jsZFwwX9tzcNWu6pxjF1srf9mfz6wYpFarbOwvv7Wzc9fzZFozsW1WqZFxve9"},
    {"Skyworth", "tv",
     "wp5WX1Zszd1fFn_zO-fZNnZj1tWX7vZGqgwGcWpd2imvUaZs8im-8vdpd1aWoVxizFYgXm2gglt2"},
    {"Harman", "speaker",
     "t-3Zq1sNdSglkm2eviNhxk8gude6brUyo5pgu6ekvW1z_gm4g_Z8"},
    {"Bang & Olufsen", "speaker",
     "dj2"},
    {"Denon", "speaker",
     "jk1h5ttev8zkfF"},
    {"Yamaha", "speaker",
     "-ce1qWftAkaan4"},
    {"Sagemcom", "net",
     "vo3zt1rVqJrLX9pTXHe9cf4raAoY6prKxhBkow7Zws6ry4WfAhqMjtr7xjs6bh9cWh1gcjE-l4qzGrY2to9hZfFop4es"
     "Kkj7yaw6fq9zx1pYCy-d8gaZ7mm3XYGxnQpq3gZ2fhg6fvGxWBaoOxwq6tp2ik2ej3ex7kdKrWl6go7-YSxyEyej6cfG"
     "d1xhAh-u7ciOusA-va6b_3jac1Yr91fi5rxl6rpMqt9qy9lX4jhj6oBljr8Xlp7_s2je7bjJdWt6pw3_m4ZaEf-9xde8"
     "ddIezo6ouq7bgJrx4bCjuVhRw_b6tnKY_GysW7ysQtg8seHpvY6WrSuzvFsYAaZDvfm6i4nr4x-3WkSaaFt-4xYk6fk_"
     "7Wx7hpOZBoI_xw6avb1kmw6xjY1bvAgXEzwW6lWJuf9qmo7v_MbpPbvd6iY9udIrec7trz7or4pW3BdH_d6hpAkt3zp-"
     "6qg6oZo8u-Bade7ogIoqKvao6bzEnrDXl1ap6sg2ZbGgsl6pvg8iZ1wf8YfJXc5jkn6ome1tmv6mYSm_8hfBgkx7811s"
     "rf7vxE_xi6gsCrZ9Xse1Zee6WzErn5r8ok5-oInds6wXAqiPyoGkyk6ofEsa4tkAhWAqW2ztk7qh1lzj7jgy8acx7xc4"
     "-Z5nr7stt6nbOci3ogFbjq6niBmm1i7qvW7woPfwVted6sre1ovk7"},
    {"Technicolor", "net",
     "wxo1gtiFcs5sq4rX8onk6qY_1sbb6ka8hNcXBZcHds6Znr7xx7whvFwAnmHavo7wqZG_hy6ycX8vaNztQrwcEo-9yki1"
     "lWl6YfLtz5qc7sZs6YmPlof7sr5hhp7ju6qaCwlSolEgyt7lmi6ysDgxUzrw7mf_6arSbGbrY7xzKet1fzl7-xc1oik6"
     "lg2uleGhZ6sih7Zy3zyHszlOWwZ7WbY1tZ5_i2up2jfl6vpX9_gMufZ6XvLjp3akx7oah7qiPzor7sh4if2dlGsgx6pZ"
     "Z1gYw8ywr6btEuX8yrcFwo5fwi8_XBdyv6ZZj8asHpnz7jXZFWYAd6Xic1zkcEcgCeql1unv6wq7tl4pXAvoW7XgNns9"
     "yiAwmz7Xnc7ZZUkzo6cgKeiCil8fXv7xeZ8epn7Yb5fmx6YhDgiz8-zt6ho6bv8ikS-KX__6dqFlU"},
    {"Arcadyan", "net",
     "_r4hx1lNll1bQvvu7msPikCciX7Xqv8tooNX-1ho8p9Wo8zXe8ZmBnzm6dfeO-as8uyv6vix7_sj9-rCujZGxtZ7-a_6"
     "oj4jx1gxs8Zul7zoU-a3xXcGogw6-vHoc2Y_t7px1rsJvjoExag8WsHhm4lnc7iwNsYp8Zry6fwi8ieZ8ya3Zq1iXu6t"
     "k8faz7akNxW3yuvFlnbGgZj8nic8zwq7WkCmWl6Wqf1ueAiWtMebl1cak6rqVmeIivt7eiZ8Waj7syb8ct8ydi6qst8Y"
     "p8xrxGeYt6s_G-hg7buD-rzFtzX1lka7dcn8f-y6rt_7"},
    {"Askey", "net",
     "rl4kd1xb1bk1lRaFWlQtweFzmDhoHuZzc1txY1rYlGZo_FridNdyee1clGtky6bXje2pmb7cgZ9bZ9vcd7ZukFeug1wX"
     "8verFixk6su8kvNmhCWjeNth9jfZ8fyqOZlY7_o3szQkrwNWssFdyW7dWnGkz_8yhZ6wjm9lX1yuwEyvb1hfWNyt5vly"
     "7evj7Zqe1"},
    {"Sercomm", "net",
     "lq3phi1dviNfgt7ilt7drJxry8ZZcc1u-Y1Y_WFXuw7oWu8YiDiyuW1obyFskuNidc7j_pVwmPfyu7oz4qqxUasnOojh"
     "1WnYUfcnGewtGsdyFznYl1X-FZy_NZukFjgoO"},
    {"Vodafone", "net",
     "fa9yhyFqo_NrXqv1asj-2crl8rZtVmYh9ewiEfxdW1-Xut2ei-m2"},
    {"D-Link", "net",
     "zg1hX2rDuInDZLnDXEsD_EeBvP_8nAvIWpAkWCyYBojZ8xvd7bZj7yYg1vWh7gWNuwb8ffk6rzp8wz7oeCYk3YsqMduA"
     "wunNeh1lHn-aGkf5vyq8Ye5_nqG-loNahd8gXX8qjY7tln7iYyFnaMuay7YX-8Xwr6pm8wbHXjZFfve9deh7_rbGix1_"
     "gmOtfo7sZ1hv1wx_6edk8YpZ9rxq7aqy6kzw8qWx6kq4zYr1sng7wny6efb1jdGpxZ7yiHgi5ort7kZW7WXpHaWx7rmi"
     "6tmq8bet8epr6WxY1eyp7gxDvWs7uWt8pWZ7"},
    {"Linksys", "net",
     "pmae7"},
    {"BSH", "iot",
     "pcjn3qmrEdfcn2rfkNjno_1eWif3kXat1"},
    {"Miele", "iot",
     "Zh7isaX4"},
    {"Whirlpool", "iot",
     "ouvn8"},
    {"Electrolux", "iot",
     "dmle4"},
    {"Haier", "iot",
     "ez1bl7"},
    {"Daikin", "iot",
     "lYW1pex8Xrgs5etj8"},
    {"Fronius", "iot",
     "iT"},
    {"SMA", "iot",
     "xj5"},
    {"SolarEdge", "iot",
     "Yu9Z-hf8"},
    {"Tesla", "iot",
     "ligOxuqX4cY_Fpgxt3vuXGujus3"},
    {"Dyson", "iot",
     "u_xe4dnut6pktW1bypG"},
    {"iRobot", "iot",
     "glkv4lqs6w_tv5"},
    {"Roborock", "iot",
     "zpdf2yzgt8vw_m1"},
    {"Dreame", "iot",
     "tth1xncW7"},
    {"tado", "iot",
     "oevvE"},
    {"Somfy", "iot",
     "nrTrwou4"},
    {"BTicino", "iot",
     "mQa9up8xakWC"},
    {"Vimar", "iot",
     "yalvF"},
    {"Gewiss", "iot",
     "tcjuC"},
    {"Beko", "iot",
     "Xl4qnrf6zxgF"},
    {"Nuki", "iot",
     "opqf5"},
    {"Withings", "iot",
     "ad9smseA"},
    {"Seeed", "iot",
     "n_zv2"},
    {"Silicon Labs", "iot",
     "tw2jfCWkW1Wwp6ZjUklHms5o_X7eiJia9dx7zeh6bb6hwQtiGhglEsuX1ohBmXh6nuMqf4qkO_cs6uyY1jqv7aX-6-zA"
     "exY1dla7wn3cfP-jj6hjw9alb6Wc5_wPyZh7rj8tDwlq7ahW8fz1u-v8-uGxouEWp5k_E-rc7udt7oip1wi5tzi6ei5j"
     "eKoDn_y6mXE_eZ1vWl7fY8an4y8rp6pk7ata6pYY8yjPpWx7nyGYZH-Z6Ywr6azLqi6Yt2qqk6hia8zgb8ybb1waX7Y-"
     "s7Yu1puGqzGbg1duGxna6or2WZe1lvg7sm2XX1kx5vvQeag6Xt5qRunSfmKgsj6uWRri4YbBwh3XWe6mq7bz1vni8-ez"
     "FrW5raCkkBqjbNplPge8wXp6le1WuJesMxvi7wvw6-fCokMij1Zkc7nrR_n6hte8pltEnXKWX7te1fb9xy-E-GZzi1es"
     "-6hAwYb8y_u7yt1-hHqzn6sTtXJokY9_vk6lqs1dqp6i_BXm3bqUdaX6bli1cca7htVtjp6nYF"},
    {"Nordic", "iot",
     "mnScWteF"},
    {"Lorex", "camera",
     "qw7"},
    {"Swann", "camera",
     "-lquB"},
    {"Amcrest", "camera",
     "-ePljgu9bhq7"},
    {"Vivotek", "camera",
     "nM"},
    {"Mobotix", "camera",
     "bU"},
    {"Apple", "",
     "pSlBbf1k3-NdRg1jPwJd9nFWCmAy4pBv4u4sFu2n6W1w1p3t2g6h2n3i2Y2c3Bgj2yp3Zm4fl1dc1zk1hh1sp1kW4gUx"
     "v1nG_z1yj1lY2Yv4Zn4h4k_1oh3lX6w8sL_gb6ir1uEoe2XW2yY2km1sc3rt1pQcu1WBoW5pCza2w2gs92gQjb6xr1eR"
     "w_4dy1Wl2eW3cr1ZXY6aW9eBZw1zaClb1sOWCfr1aOzTb-4ru1cp1dlCi4sq7-1Yn3nFWHeq1rlb6g1ra1vx4mLlv2lQ"
     "gw3uOz3vHju5dc1tf1uIds3nk7jd6jc2nt1ft2ab1tY2WB_pc6i_6jg3np1x2kc4i4n4uzEXn5o6Yn1kSs6vKv7sF_b4"
     "yX1sRlj4c1qw2kQue1lke6dW3-Uwm1f8-b1uW2tc1gZ2kY9cs1vm6sNqFxf1XJyBpq1j2cb1YBfLhBtTwb6ku1ob1up1"
     "Yy1nC-Thf7hjh6ir4pB-q2X4Zt2my2W7js3ff6rIkl7eCzs3k1pb2lnB_9a2rp1lRhZ1qZ1csa6bA-_2pOed6zj1ax6s"
     "e1jc1tb2lr1ln1kv3ez1-Ou1cc3Xh3YX2WoBYX1XX4fSlrY6dj2tw1oe1Yx4mc1_c1fe1ii2Z_2Zx5le3-HZ1aa1ht1h"
     "z1rk1kl2tu1dNih1vq7uc6ik1qj1ir1nj1rde6nRZf1ys1mrAjm1XLes3axCjJWb2uz1cj7rn4Wi3Zl1tzY6Mue2Zn8W"
     "y1uHor4hBfW3kSj-32tf3u-2Zv2g8rX4rh8uSnEnc2Fek1xp2W9Zq1-IeJr6fMZ2cs3jcc6kd1iHe7_o4t7jZ9en2gW5"
     "ot1jc1u2fu4wu7jY2XChY1Z-1og5wf4i2eXc6jf1XLfc9lk1uy6tY3dx5aw1on3Z3ey3Y-1vb6ma5ZJq6aBsp1tE_i5d"
     "r1moY6ch1yKwEcOYr4kPXn1ib1dX3dx3ec5qi5np1ek2tv5sg1iIqr1kp3jPxu5yY2vu2tZ2mIja1j8jmX6th1lg1YeE"
     "mw2tq3_R-9kt2jj3ve2e4cy4xa6jt3ig1qAZp5ms2ck3zYZ6dDhl3pd2cY1ok2iH6bh1ah1lDdo4aIdi7Xy3ilAua1o-"
     "4t3_g3AuT_Y3xOmq6tug6hl2sOYl2Za4mu7cZ1yW2im2br1uY2pq1xRnr2yb3b3cZ2nr2lb1Wj1jElv1l_2ay1tx2gsX"
     "6gk2Zv3hKip2iZ1Zv2ug5n9gz5beBmk3ZKct1e8Xh7dn4iD-X3bFhY2zBdf2ctZ6aW7_t3W4ji1aY2kX2oe5oz4dfBdy"
     "1dy3es1zt2za3to2k3dX2dsj6yl3XUeX9j9Wj1-p3tc2sb1gNw3pf1"},
    {"Apple", "",
     "vhZv4po2bX2-w2cFaZ1tx1cb5yI-n5m3cek6yUrq3vf2wW7hs8m3lo1XEgz8v2lo2lx2mh8mc3cJff1i7wCbXb6ci7mJ"
     "z9gAwv1a8bs6oY5jx3qu9mj1qu3qr2_wAXFja1XAhtd6nc1bW4ft2W-2xl2rg4oVr1W7wm3fDdb1ac2_x2na5gf6ZPmf"
     "2al6cw3hX1jqe6bp2W1eX2bFfl1vo6-r4tAq2as1ls5vu5iKoSdX1vX1l7sAaClHqm4Let2dZCyb3iGoAmkY6xUea2gx"
     "7_FtJqu2fh6zg3_W1yx1fm3dIs9j9xy1y9jk1_7dLae1yb2dw6Zq1pd1_l1ki2fTe5jn5a8qMmLr3aJ_8oeZ6w5xx4ff"
     "4nb4yo1ql4t5Yo4aOdq1aW9je2uFZt2Yg2bSuMku1oD_j2-z4bre6Wf4Wf5l6ps2tTbj1dBpa1sc4eTkh1Wy7mr4rl1Z"
     "f2sCaAnLYOab7up3kKsj2Zk2v4Yz2vRvxa6mw1gk1rx6kRxCzp2kNiv8eIWy2ww3tv1tEYZ5Ym1uf4-w8pKecg6WJY9u"
     "o3Xq3gt2ZIZh2pa1_k2gY3Yg3nNj6Wn3du2oi2lX3tx2h5nX1rY2bm4Yi4gY2kPgKyGdCZc2wgb6in1b9zj1bh3Y2xW2"
     "wm3gKp3Xq8_Z3fHoUki2q_2qHpx3-W2wr3vr5ej2k-2_9oq1aec6svBZc2fc1s_3fy3ql1tObY1Yf2cY1cGtMim2Wj3Z"
     "h2qZ1ZVop8fv1zl1_Z3w_5yDz_W6_Pkd3xs4sc1xAZZ4v_3db1y3eh2_x1oX3s4bXBny1oj2le5n8XHsCeTkc2j9oQbq"
     "5kTkNoy1t9uYX6nVeW2xk1tt2xq8Xw1yIZ5ls2Yt2tj1db7pEfr3xK-A_t4Yi1a9ig2yZ1f-4tX1hY4cY1sKb_b6xn2z"
     "X6mG-j1-c2of2nu9pW4xZ3xUi6WN_Bhn1zd3cl2y7tBcUn4ag8aiAo2qva6sx1WBog1dAi3aX3hg8sZ1ZDgY4X3bJfc1"
     "j3wX1cq3-k9-f1mY1ga1wg1wAcw1uk1af1_j2vv4_Bmc2_iZ6zX2Wm7jKpZ1il1Xd9pn8p7r7nl1jOk4qc2t7xDds1xk"
     "ArnA_n3zSjig6bX1vi2mt1pX4z8ui4tBkAXW4gt3k1rLsX2f9Zm3x6nm1om2Wa1o9nh1XDra2YX3rx3-W1lx2tZ4ipZ6"
     "h6dEc-1ov2nm2kZ5ac7-cBkr2zd6yNku3znAe6nZ3p1mlY6oOoSbo1vPwz2g_7fp1rOan2zr2_k1Wc2vcDsy1Wi3gh1m"
     "g4Y4ec1-v1br1vRWy1oY2fl3j2gmX6wf5so1uX2i4ib3eZ2di4YZ2e2o_1rW1Zi4"},
    {"Apple", "",
     "Zl-u9ys2Xt1lr1sx5lk6qu5ta2_Off3b1Y7el1hCd1WWd6xz8as4e1ZTXa1af8Mny3zk4p7qKdl1ce4sj3k_3Yl1x_2z"
     "n2j2hj3YkZ6vf3rp6hg4W_6cb2jd4ajEkp1ov2g1lSfb2pFc2i3pt1aY4pZ2-d12vAW7pNzsd6rh1ZX3_i7Zy1jk2_1r"
     "3rPrAfk1vp5ca1_g1h9ym1rDaBjg1kl1zv3Xz3v3uInbFbUzkW6xt1_p3X1nt1kJck2ns4rd2xc1no4-j1qg7oTpTW8w"
     "Aey1pr1zh9nY3so1bY4qe1cInt5boc6rCls6W9lj2sb2Zj7uFuZ2iFyo6hi1wj3l_6wMb_2ku2ob2d-1Amp1lh1vtf6Z"
     "5yHqd9kv2Zj2kBpKhZBuf1iy2wn1Yl4glGnh1iX1tsX6qW1tY1Zs2rRwg2m7za2Xb3-c1yh1rPo8YSwn1bW1-g2Zo1Xd"
     "5tSrn1uu2rPnr8vm3io1lr7gIqc2di1mb2YmY6as3iz7yUre4fJgSXc5fb1fc2ax2vg2Qgb2jd3t4wj1tRZw3rNye5lc"
     "4wu2yp4xZc6cSYi4c_5ow7uZ2kZ6pd4Zh1oZ1yl1Y6lX3fX1Zi4ze1rEbAmY6vq2sXc6Zu1zE-JXY5p9sk2Yw4qf2zg2"
     "_k1xs8s4mW3qx1gX5to1e2qn3kFsWAac2yRcvf6X8in2tf2wf2egBmg1mg4Zd1Zb1w-9gVnu1xz4eZ4bh5zna6p2QWZ2"
     "ky3sGnNtAlLYU_h4jf1Xr1_e5dl1sMw8mX2rn1mX2k7adF_d2zFaf2gci6wq3sl4zDei1n_1st2ZDyk3uFdf2_a3qn1q"
     "a7sp2rz4jd3if3Zj1fY3q3lFun1fp1XY1zmg6mM-w6-Ftu5Ya4wn2z1cFgd1o_9ai2ze2ZuAjq3Yz5_u1iSpfW6iX7v7"
     "mt4ip5Yk1b_9Xu6_u1gz1_W1kc6pNzh4lHrp2wFvw1ype6jY1xVps6t1pz2ho2vY1jc3j8rQrs5qW1qr3qo1-b3mEsn1"
     "pNxn1wX1hY3po7_i4fy3dgi6bi1u_1tzApv1_q1dZ7-c2lp6YZ2o1xY1-5ePoGuX1zRpGck8cq2XKqdY6ib9jg1ZW1oY"
     "5Yh3sIlc7qp1xd5-h1v-1ZDZ3hCbe5lCZm3_f1oY2tm4bW1Wxd6zJmo5_f6XZ5ga5Zh6mv1v4a9vVz-1c-2Yo3yx2wc4"
     "ZZErMs9zgZ6We6YKwPc3l3ab2cg3eSbfBtk3_Upp4bh4nb1_e8nn1Yz1ef3eh5djX6ct6kb1hPhz1l3_e4y_6wi6uCvA"
     "zv7v89xi2ji3bOj4uh3WCqTyZ1em1sBsO-AlY1Mg7ui4Yz1w_Z6h_1Z9zZ3fn1v_3qFv9dMlFjd6sl2"},
    {"Apple", "",
     "cjveFcu4ue2Y_7gBtc3oj1pg2xd3Xb2xw1ad3pa2t_1jd1vfX6ye3sj3s4qb2lQwKau2em2Xu2g7Xz4cY1dh2bFvHti2"
     "xp2vl3p_6ll4fw1xc2Yp3yRWS-X3sn2wZc6de1y_1zg1Xs1xp5is1pr1uh4ug8Wa5h9s_1vb1py1ac1mh8an2iv1mr4"},
    {"Samsung", "",
     "m7eCpf1yq2q5y3YHW1v3v11l3k7Cw6xEk8h5Y7n6d4a31g71iAp1b41dB1_2z11z3y11u2l2k11m6c12ne1q_GWLyEjY"
     "2k8ck2lsBni2pa1mZ7kQzb4oj1kld6hTqh3pZY1eKwj1g5oPYg3fz2im3xh6hWX6cq1n-1ya1Wc3kg5ho1WqEaX5pj6t"
     "d2gc4Xo1go3-Z6YFzl3c4ljX6Zh4my6gKZvFpz5wb1zs6op2haBu9wsf6gm5h_2sCce1mc2YIuYFgo4bd1cSZh6X-9uD"
     "Wy3_z1ncb6io2w-4vw4lu5kb3lxCed3tY2eX1rQhk3gt1lb9jW5_qe6jOwc1bNf7sUId-4we1ms1__1r5am1at1z_2aH"
     "zg6gn1Yk8Wf3sx1Xt3kX5qrq6eh1Wx7wZ2oVeY4ez3hZAb2rnD-4pr4muc6-FYb4dq1bh1xd2gk6ck2xf1gp2tkNzu1l"
     "Hil1gih6g8qu1gs1wa3io9-3qv3-l1jw1m5-gAao4Xk8Z4tg5cd5tPWDxv1plX6mX1tb8_i4aZ1aeDlZ4cg5fq1-X4px"
     "2gx1Yl2xm4ja2qyf6Yy1qqAkTqhLsb5la3iy7deh6vm4ybIZZ2Yl2qq8lZ5aY7-GiHlDW_1YAfy1jmj6xw3pg2aW1qjK"
     "sz1mW8rx4oX1wBlY8eY3X2nea6_d2i5ny2Wu5Nua7an7pEly7fNXIch1Wp1cw3lkAvc6qXc6Yh1at3uy1ja2dW2Yl9hM"
     "f-1-lAle5mt6_b8et6Xtb6-PuZ1hz7meAubTsp2tx2_gh6WvDnu1-z1Za4os2vs5bvDq_8fn2bvd6iZ5ey5bi1mc1Yz1"
     "ba4_w5he8vRmu7rr2cy9Ywl6et2mMZv6eu3b3dsFuv5vb3p-4_p3-9jcb6wbCiv2lW3qt1xm1cY8ap3mg3k_2Xn1xt4Y"
     "f4pW1pb4ev4jY1_x1mza6kg4vf6tk1sVhxGmr2qY2md7th1aW5o-2ob5zX2oLXye6lhMvYBss2yp4WIman6gp1qi7kk3"
     "le5h2fa3oYAau4zn2Wb2wi5Yk2az1ie2Yb3fe1ed2p-m6_iBlRbX3f_1zn3qAym1oy2cUzq2rf1he4yt2fpBjZX6tZ1t"
     "-3YR_B2bmE_v1ad2wk1_s2zX1Zn3lt8mn1YNXJZa6Yu5vid6ck8Xh8nIc_3q_5er2X2umGWX8--1iVye4XzW6XsB-2vk"
     "9ex6hp4iiAov2u_3mk5tj5-lb6hk5lg2gIhW9i-2nqLlg7xqA_W1a-c6dj1vs9mZAff7o-4haJXc2vHanY6zv7iQcKvm"
     "3mQkk2ml1f2wLZY2aw1hv1doFZj2eQZw2_m2iMdu4rk2ph4ci1Xqa6dCYb4wq1x-2mk2fzAtc4nQXQti3_Ijh1ZeClnD"
     "w5cqZ6hj1ex2f6"},
    {"Samsung", "",
     "zdeW8pd4yy1wl3gt1Hij1l3zJqo3oY4_h1xy2gJwBuf5asBqZl6np1d_1ae2mKY6iY2kj2Z3fg6aX1_g2YhEWW3xc4kz"
     "2rlBurk6_d2vzAaw5gi3tW6sX1qSls2px3wht6pz4f-Ezz1z_1il2ab5dx2zd3Xx1yw1gn1zNvm5ws1ea1xw2uvY6jg1"
     "WX1qn3_rIko1i_5-p5ng6-c1bbEZNYp1sjY6wWBkv1v_6XAji4ct4mf1w-3fg9mPtv8uj2ni1Yyd6co1j7ha4kx3cX3w"
     "o1Yr3dX1znBxKYeBnX2cq7b_8nxX6zt8kd1tVkr2dCXe9Wm1Xm3rw3eh2bb6eoBee3aq1ty2web6YX1dc1zu2tl1Xn1s"
     "pBap2Xe5hW2Za1goAqY1eVuw5-Y1ds1n6Z-h6acAtZFec2up3dn5bSgGfb1gm8st4Zo4jnc6mc4fd5Ze1kTZt5km1rd9"
     "zPqJqi1-m1dW6lr6mY4fr1Xj2ax4jW1-Wh6Xy5le7ot1gf1og3hZ5-rBXy4axAXfs6hLim2gZ4Wu2tlAmuAo3Zw2ri3f"
     "8Yf1rW2bq1Zeb6aPdp2va1hW8qr1tj8nk3_Uah1xw8qt8go3bz1qg6_aw6xKeW1jp3m-Cjx1a_2cz1i5pn2Ww4zcj6mE"
     "gW1_W3ss1uv2qk4r6bGqu2zEWp7jTlQo8iUqq1kz1wn1vh3he1eZ3v6po7Wf5fj1ew2Zoc6wk1WY3sh6Z_2pb7zf5re3"
     "iDmi3Xr99ie5Z7YHut1lWl6-Oxl9am3ex1rm1dh1qX4cY1xn1cw2qt2vn6hfGkse6kJ-h1vp7we2uz3pfBrm4bp5q9np"
     "4vLSdo6hhh6mKXe6j3eg6j_2sYAY_TqRin3YE8iY1ZjX6n6wr4_Urx1cKbX3na2Xe7fNta3bf6YY2cgAfa4z4rj3a-3b"
     "e7tcb6Zu6ynJsd3u5fDdy4bY4obExF_xd6fRrn9cz8wJxPsh3Z1na2qy7hX3fo1ji8u_6r-u6yj8mRYx1aRmw4hCgZ5l"
     "r9gY2mLig3qp6rWZ6cmb1YTZi3Xd6tZ2ra1jq2uhj6rCkb8tm3s-5l1ka1ay7a6bc5v3Ya3pb1n_2abCZX3ys1hf1-Fj"
     "bY6ui3WdAoZ5dGiW1qf6Wj4-y4Y-5zl2hY5Zc7brg6tt3agNhW5ov2on3hv2hwAWaf6sUcc7kz4ab8oBpn2ZGvW1ba1z"
     "Hg_5xrGrt5uQoq1qw1-vY6cCmz1gY1gh7Zu5XuBkk2nKbh8qt8pz5hOZm5o_2pfX6ZvEdu3se3gY7wf3rm2sjGzg5i-2"
     "vja6nh5s9pc1gt8tn8fo2je8xUpk3Zd1ra1-Z7yw5Wx1cw2"},
    {"Huawei", "",
     "Ya6ki1uw1s1ff2tn1Zd2Z7dy1Yc1jW1pw1cT-v1ec1o1WFaQnz5rv1tq2nX1mDoi2-b1ub1dg3_Ogk3za3ja2tMtl4mD"
     "l3s9jcY6oo4XX1pf3pCd-2g3qk5l8bAxSip1um3kc4b3jZ1-9ih1bY2tTa4mv4oY2hc2rf2qQe7tGfBre2lEvt1W_4fb"
     "2h9fFcl1rg1x3ztW6-c5ZMtAfc2bThs1pDYDPch7wFtu2cw1xv2tEve2W6tFpu5sCui2-j2qy2hY3h2vc6zX1Zp2g3dT"
     "YZ2mk1mPxqY6us1eb2x6_v2dc2ZGwBhGWm1jm2nUXl2ez5de2yr4q4k3wy1zY1Ws1ep7i4qE7-2bu1x-1n_3Zl3r8k3r"
     "l5_gX6rW3ek3pb2sm3X3sc2ib2WSkCaZ2hA_u2dk2zg2ql5_Ehv1nd1ha2ri1hE-e3-n2yf1vHqJrv1rd3gu3Wt4buX6"
     "ar1_m2lu3wd3jw2oBri2eMb5xw1qZ1c5bBzu1u6sk1sk4hq4_Jvo3cJlVtHb4cBel1Xm3b3tz5g2h3bd2WSlc3eZ4zrX"
     "6Zj2dq7lf2nf2d5cd6tl6nlBZ_2eY3bc1b1cc1Zi1wi2oQdZ1oLeg1co2oif6tc1q3b9eY2gKg7nk4en2x2pi1X5iUip"
     "4Zd2wc1zd2jSgw2mv3_x2cb3gY2Xj1gPkhBr9q3-h3__1ZSWgY6wMzd2vg2Xn2ZSnl4nk5tBgh2gv1cp1u_6wa1tw3Zn"
     "1Wa1c7vAfKkRw_4ox5xMsJoj1tY3dHfx2kjW6r5cX2yz2y8vZ1wb1sv1l8hEoY1sNy2so3zZ1aAvBXDiRM-b4wMv6kIv"
     "Ece1xLiPvr1m8d_1hKq_3gi1cx1yk1b5jJ_g5xf7vB_P_3pPjv1n6oAsm2Yh1dqb6ze1ot1ZX1uh2k9ka1c5y9cGsDql"
     "1Yd1c1WUxPzg1ge1Z9gx2rc1YX1Ym1W_2sk1cn5d_1aY2pMwq2n_2nz4YOtk1YHfZ1YJ-l5u-Y6k9tNyBob2eX1_p1et"
     "1gy3_SW1-_5aPrOvr2zi1ms3aY7_Qjh1aPrw17YRly2o8bj3lTZp1Xy4jk2Z7d1wLhHltf6d-3wZ3kFZl3eW1zZ1dy4k"
     "f1ck3lh1bIct2sJnAzMx7Xw1h1dX2uSe6j2fq1iAhn6lZ3aRqr3YAsb1dCWX2Za2jDuKcbW6rl2un1fHox2sy1nOaj1Y"
     "y1nx1Zg2s_1YLyr1vn3fY1n8rK-Mm-1gd1rk1cp1sX3cs2hB-l1gh2gAgp6rc2ag6Wm1ym2wzZ6xZ4iMta5XFyp1q-1p"
     "a1ZCrZ1hZ7WIyJmb5qX2ta2Yx2iz3"},
    {"Huawei", "",
     "XWln3xa5hr6ko1yo1u4hKW9wgY6jn3WIlm3bZ3xl1rDot3uT-h2vh1nW3xDx_3y5to3co2qOo-1mChW1zGui1ze2lt1y"
     "u1fi1sy1Wp4dZ2kp3sBxHz1ug1yoX6ba2ql1dX2-_1lBwf5oi1XUs7g-1qDcX6_z1Yy1cp1mk4rx5eu2hi1d1kb3i_1z"
     "d4yp3iQm3mha6hh1kW1bKnn5ob1ne2-Eqy8J_W1ah3dNtb1lx1zEqr2yc6bSo7vApb3Xi1co2xf1m3yBgMry3jX1zk2j"
     "Ufsb6ZGkTsZ3t1ax4oEhe1dOaGfTjNxSzUnRXTm2tHKpKz2yp2qx2l6gf3yt2qx1ai2iKdn5dg3jt2fa4sEmi1tm1z6q"
     "v4xY2_8iPdC-ne6lx2ya1he3ry4YX1bd3zCo4nd9nQaBWc8qJb7ff6rAXd1Ia6ea1_W2sW2tm3c7YW1eFcnX6k3_Mhp1"
     "3xi2oc2vY1dq1zs5YHuc1qg1nc3zf1sk1FqQb9Y5hs1vf2Xf4k5ZNy1mn1uBYg1_a1iMuf1ki1qvIhBzfY6lc1rq1rHz"
     "Kb2jm3y9m8XKk1pe2ak1md1yo2Wd3fBnQZ2jOew1Zf2jY2zB-n5nZ2ou4y8a5co2rj2gj2ju2vn2xQwX1el3nQpRx6cY"
     "g6jo1Yi1cp2bh8zi2ox4ry2mUfZ1oz2zAm6zp5mHz3dNbg2-Sll4ut1Zz6xk1m5tDx_X6jZ1pEYEYw2Z5vgCmX1a-1ko"
     "2yIZRx_2Za2cZ1dz1oY3gg2fk3Wz1hSf5yg2cOoAr8Wa1te4fr3y9dd4dYY6_o1ka2-C-l1yu1gs3pKsZ3ub4wBwVW1y"
     "P-y1xA_m6ae1vX1k1mx4me1ou1_Vi4-Svl1Zu2uC-b4qe2lt1m2_2kW2fq2pe2ldZ6_W3bQbX3-X1iLjLrCx6kt3Y4ex"
     "4iFmn2mPjq1Xi1Uuz2eo3in6o7dUoOby1xw2ePfX1kk4-Ibw7t8euc6hc2st1Wh3if5oW1h9tBjw5kp7X_1Zk1ti1p8r"
     "e1dPqEu5qR-Txj3hq5vb3Yd2wv4fgZ6kFyIui3aEZY1tLge2xo3jg3iIri3b1fTZw2_u1WX1Zk1bZ1vDvw1wNwrCiIv4"
     "Zm6wY1b6mh4YQvhh6cm5gk4vi1p-1gq1ak4tw1ZGY6dBc9kn1uIx7cf3jLXW1jm2XC-a3tX3us4Xv2ys2ez2kGWj4ZzW"
     "6vl2jGjk5wBqX1Y-2v_1fe2WY2WRyX2XDmo1eMdb3bs5y7Wi1_k3cGqMbc1vY1aj3ZCdb2kc4ec1ws4mw3eRiZY6cFc6"
     "-5nl1xY2b9g7gCeS_-1dk1jIyo1"},
    {"Huawei", "",
     "qbjm7_d4f1ti4j7aOw7ul1pX2zi3hw2ZKwZ4yf2Wc5g7-4cW4iKhr1g6vw1zQuEi5vd1we2rg1yb1c5moY6jb3nc1_Y1"
     "sPWBp6-Aeu5jH_QcTfAZn8Zm1_Go3Ws2zJrs1o_1YW1vt2tTsi2d6_u3YY1cl3zd1Zx2xj1pMic2v-d6gp3Xw6R-k2aR"
     "ce1bb3ku1zw2zd2fl1-Ig7mr2ou1lgCn8ii5mSv7gW1fn1nt1b_3Yh2jsc6-W3hu6sCiZ2d3me2gZ1pn1hk2hOYi2jc1"
     "kb7x5rW3lf2W7qSwd4nm3ox1fW1cU8wk3vd1X-3loa6d8mU_2kt4ym3uY2i5W38yX1zw3n_3qJZDXBmV-Dii1Yg3gY1i"
     "Pvf1y9we4vGdl4ZW2ZPZIXc1tw1x5-4ip2qT-KrQZk4kBsCWeZ6aW2pI-l1lF-g2nVXw3t5mh3qY6yVzh2XTyCnp5YIZ"
     "b1yw5i6wM-SyZ8cW7tk1tu3xH__W6kGsT_-3b4j3Sgn3Wl1n_4d1Xo3vW4fp1lJsk3zy2eW5m3nPbg3o1wY5rx3yNvr2"
     "nu7iPd2vtX6iVhX1oIoQy3hs1Xr3W7g7ix3kJyd2rq1kJqd4cf1xu5fUZKvq4kb3_TWFyKiW2ih1-y6XDz2lLnKjq1wQ"
     "iUwDmP9xFqX5znX6gLeo1ad3co2Yq1xd1rl1ph2kh1cToKst1BuAvv1up1gW5xi2rMYl1W8h_4dMtY1ej2os1xa3jw4-"
     "W5ka2pRxwZ6vp1ix4vz2bu3rf4lk2-Pm9np2w_1fx1sHg5r2qf2ya1zY3hs1jGV-s1dLwi4s1bd3bDzn6xUgVjd2kWd6"
     "hH-Vrl3kg5-a1sl2u6gChCzUgz3qc6c_5xY1Uc-4_Idn2mDxt4vu1wp3d3em1c8ah5lo1wmX6nc2qf3a5pz7g7cNrX2z"
     "1qPmh9WCmU--1eElDsp5k3n2fg1eGye2pq1n8lm2uY1kQd3Zv1X8y7rQ-m4w4vYh6oL-Bil7bg2dIZW1tRZw1tPqv1_o"
     "2kx4Zv2qMj6kX3yf8r_1Xn3WErDpX1XIcm1_Y1X9Zw3qX1Xt1Z1gFcQkm2bzX6Yt6Zw3aDp_4sx1wOfZ3rJi7xOja4ad"
     "2yv1gi1jJ_P_Du9_j1iq6xj2aY2nk2bg3dQaGj9vf1lJcq3xl1geY6jLqv2vu2YPlo2k_2f_1hIcW4ph1i7s_5-Lac1X"
     "n3yz3-4sy2-q7kKhBWRtGWq2Wm2lZ1gh1cCkj3wY1ioY6rz2j1nb2rl3va1a_1s8sCaY2Z3fh4mY3cc3ya2sZ1xv2iLc"
     "W2ZW4ky1zz1Yr4"},
    {"Huawei", "",
     "v-mfBkg2lc1l-4d-2lOp7hp1_SZcW6yZ2ZMYc2Zc2_Bdf2ZTub4Wl6Z-1jF-h5Yz1mBxv1de2qj1sy2lY7gx1-f4pHoV"
     "fc2bdj6kIXTxw1se2XW2kq1gGfd3e1kh5rr1nb3y7r2jb1fW1kW6dDZ9-Eea1uBsr3Z_2zw1ug2da1_Y4dze6jhBts2b"
     "1eBjr3gTfi2tk1rp3eX2uy2t8bn1-Y4gW2hk1pFog2mAjs1oOKcZ4jBuMpl1vJXQcDzH-Bjx3yEg1aMul1WiX6xAi5dp"
     "1sg1ySW2c2do2tn1uTte2mo2ok1YMWX2kj1WMvLsUeh1jW1_GXVvW2uLXQYY1as1_m5aIYJqItq1Ww1xs1lIrf6WOyCm"
     "HtRiW1wl3fv2rR3cjZ6fy1jx2ha4ySdk1od1mn4d6fw5n_6qb2qRaUzY2YGyY2zw3t5tVx8YRyNhB-l1xt1h2qh4ZMW1"
     "txd6aOxk5X2rFgm4ho1md1lq1rw3ib2bY2ub9c9om1b6bg1Zy2bq3w6pl1l2hLlCj1u5-y3xr2gv8gf1ddX6jBpW2zb2"
     "_t5fb4zX4_r4oZ1tVdW1wD-Vrv1-Jk6ks5ky7-l4a1ik2uv1x1ZW6qX1Y8-OZy1ugm6vg1-e2pW4qDyD-z1-7sLhc1k7"
     "_f1ou3yv1-Y1e7rt2hBi8ls1j_1ss2rf6tRmj6_w1lXY6ma2_n1su2jd1sZ2W7mm5pz1mAiv2x5ma1no3_3bc1e-1lEk"
     "n2dy1vJtY2eW2cPmn1uj3ef5lFuPvo3nY5gmc6nc2ra1mv2bg1rm1Xm1hy3ur7_4yd2Wy1g7lW1fq1u6uy1W_1xG-6kz"
     "1bSft2fs7nb31dAyQsBnG_s4wBaea6ij1rr1f8zu1oW1jo1re12WV-k1tX2ni2W1y7Zq2tLep7Xp2mZ8sW3ll2d8om4c"
     "-2-9gr1kn1ip1WKqz3o8-qa6h2f4YJuc1ps2gc3em3r9-d2_m5-z4_n2t1kW1_v1gTs8_y2u2j8rf2cp3Bc7nY2z8nPc"
     "j2sUYb4mc2f4nq7aIgtY6Zt2av2xl2uPYz4jm3y2tEoZ6mk1pX4hM_Ueq7oOg3ff1m1kj4ql1eh2un2GfMs_3Pun1lk1"
     "x6bLl2oOZJgpc6nQgl1og5ZEfJia4en1jDd8Wa1xq6kt1cf1g2ux1cStf5-Y1WZ1fl3j9sp1tj1-g1jt6uUxs3yub6Yf"
     "4Wa1vm2-X1gVc-1WRo2wj4ju1wBnp1_XDWSvd1vX1dZ1k_1sk1vs1Xo1f2uUb3vv3qb3bw4LhMWX1yzX6es5Wv1pBZs4"
     "zc2gVtt1bx1uDji1v8wq1Z2Zd1ag4"},
    {"Huawei", "",
     "Wh-eFbp3pAwh1Xh4aB_Rd6nb3_f1p_1qy1-X1dj4sDig1s2XDua3kf2fdX6o3go1ig6fOte1n3bIWMc9vW3XKs4gX3xG"
     "lo1b-6wq1_RuX1Z_5nHcBs1s9p2ja5jHtk3ra1rs6-i2ez3zxZ6zo2v4tk1kUfBce2yf6ph2WW1t5d6fJ_6-_4eo3Yl1"
     "jY3v8w-2u8-b3hCoe2Xg4rq2Wo6sC_X2yY1Xx1Z2"},
    {"Xiaomi", "",
     "esd1YY9WeAXZf6stLgr4yXDWPWr5by1kD-Y5ssj6tb2qmZ1cbr6mk5fqUbsBhs9-h1jxj6na5poW1zXY7crHac2ZwFvk"
     "7tyY6bwLgmBajQnaj6xf1_fOddGsk7Wvk6sq3v4vn1cb3lsAtW4oq5wb3ipJjcd6ovKjrQpW1ewm6arGaWUul4uZg6uH"
     "jy2guj1flByiY6ZdIYXp7YY7XyHwcEpZ5rm8dm2nsg7We7mc1jw7mgh6ig6-x2gwHfYCdi3vW4Wvk6bcAWoy7xr9eh3e"
     "j6XaJbl3uz4kLzg5mfj6vrMgoTzZX6Z-Hl-2_j3erApXElq6q9p_3a-o6rh4md5c_8un1Z8-Olk1m-1itCX_1Zx8jro6"
     "pxr7_X9hiSy_Cr_b6zoFz_Z1-a5ykY7_ZFyzIZ_w6aAtZBmh3lUpp4xz5xj4j-x6cmN_d3ny2ny6yrf6hv1xpEbwh1xl"
     "g6oc6og9kzDua9iis6lk3-c5ui6Wge1-_Z6xXKqoHvrF-ug6j2cg2ed4Ye3khUho7j-4gu9sYk6skM_h7cfEtYu6hYQq"
     "bW7op5dd3rh6ndJco2iXa7rv1bw7naCm_4ql4af2Zf1r_o6akJhrW1Zzc6jxIXg6oX3pT-yHlzl6wX1unNlvQpm3jy1Y"
     "c1qmh6gj1W_A_uF-UtAxm7sko7jti7cx2yr1nZ1gg1WzKWg4kg2gUhf1qk5ue7sXh6nrFdpCnjW7ejRejDgt8xu4l_a6"
     "Xaa1okHixe6iwMoj7zoHmsz6ie5eGa5ZhGnjLxtz6-f2pc5ltb7nbHdai7rhNtBuo1ir8tZb7gj5mt7WqOn_m6psFbx2"
     "yYQop8zE-f4yei7hi4wz4Xe3Xbo6tk7oo3Wz3Y-2_WEwY3yrDfs5vlm6oaC_YC_Z7dl7wf5qn2_f_6dYJroDbmi6_sGa"
     "m7glDZr5dW4aol6skAXEvlFnm9ck4kt5r-1ss8nWp6tX4ff1xv7qg2aW4mk8whFacc6-Y8bZ4cX3XxW1zjAXmy6i-Hpp"
     "v6ll5tYCmoAfrQqmp6zeBwl4vg8Zv1qur6Zu5ijAdY6kf2xfHey5hy5"},
    {"ZTE", "",
     "hl5xUjb1WX1_JxEgy8wZ3oda1alj6zLdoJer9drW7Yz9tb1mW7aoEY-3ozEh-3bwY6w2zb4bd9vh3vmBejd7lIfiAoYb"
     "1o-h6tf2mo5xr7Xk8wu2yrNtco6vjCbf6_o2pi4drDZm6zYt6cYG_fe7XW2d-3wq6ov7fxBytNbq2dhv6ki7rY2jWArj"
     "6tX1ez3gfh6XZJwlAoSlj4fp8xg6cq4cTsa8dyf6giImf9Zg8vtEepc6bv4if8iEhr5gYGx_7xh3sh1bx1Xh2sn5Xfi6"
     "nc2bg2b8Ys4r9wKzy6or3-n7uj5eqBzRxKhij6xk4pk7pq3jl6dn8v4uf3Wz2ny7-p3xh2p9o_4ic-6gq1fiCYf5wg7e"
     "X7-SZeb6WjHzvl7oq7ky3pw5leGrh2lZKav1WZ1rZg6rhCxn1wtDrZ4qvBovi6zf3u-2cq9fSavNEh_7qqz6nt6g5lq6"
     "sW5aq8XuCduf6un5hR-Z2Xr6vsCi_2vV-hDlX4nY4fdx6wX5ps4xv7keGdq6bd4Z9zZa6_mAYy4l_Bcw9wt5axBtY5zb"
     "b6YX1hvKfW2ymSqih6qtMWs1b_3_iDqa8yY4-uh6jb3-RzdJZ_3pPwg2_k2dEgky6eiPj_4mX7Yf4dy5oLqol6qe7dbF"
     "aZDWZX7oRgd3xr5ad9dt5oe4jW4ml3i-v6yg2tj2k6ie8hWBWp4Wl1bvAlu9zaq6ey9cY7flDl-q6Xo9mtJs_2xZAfXE"
     "xtg6aKjo1fX8nyDs1ow7luNgPmWw6nf7fb1Xs7-s8xm1bt2Wom6jQdcK_a4cb5rlJ_Z1s4cl3gnh6ebNqZ1uY2drCkk3"
     "e6wY3Xc1Wa9mmZ6id7w_RYv1iX2f-5jY3XX6yZd6fs4-b1pz9ip9bt1akBp-Kyi1l8tmk6-Y8lz4o4bx2kbHho7ns5oW"
     "e6fw1tbHwaFjnFha7klh6vj9omFYeTWzZ6cgFZZDem9y-2eps6cQiYOfdIgit6WuQzj5Wg8Yc1gW4Zqm6lW6qwGmw4ok"
     "2yy6Wv6zxAisf6eZJfrW1rb2ew4ykf6huAfSph7etMxsDiX1fs1llo6vW2wfGtGqv2Wf4ZaJrXf6lh1sdJxr7ja4piAc"
     "u7vZu6dn3en2qq5jc7ga6coEYWi6Zs3zZJxd1wg9et5ys2ttq6-wGn3lKad1uf4ziIl-2io5iz6p3ynl6rw9fJrg5pp7"
     "eb2ae6_r2zl8ny4fna6dBco9Dod6ux2vo5va2fc4ru1wi3xs1W_1Yl2wiDgoc6akBml1nw6jt5tb2_qRYm1Xt4xae6ht"
     "7Zc2nuEhe8mb1vJXn4sX3rAat5ycx6_o1Xj3wa1-o1uZ4tx1Y5lmFdc3rSeuk6vrEou4g-5nv4Za8rr2qa2YqCyjj6yl"
     "1wfIcj1bx1Zb2f_1sdEk3kgBpwc6ps3nvNqp2so9wlGXkf6zW2na1-h3YApX3dxCe-HzNcWBYTul3Ym1dja6pcIrl3Yp"
     "3km1dnAvYFsjp6hr4sa1-iHw5tk2dv5Yb7giC"},
    {"HP", "",
     "cF1-5bIuOX4a2mCsBmEY7q1j3X2Y9W3d1x3q7e5XD-4r6p6y4j6j4z5m4j9iAdA-8nHg8v8a8o9Y5vm2p2g-7bX4xX2q"
     "x5X2Yx6wny6jky7soBiliGwKnyKmgCgey6foSheb7XdIvcc8mXgFt_K-kFWYX7dn4-xCoht6za1Xh6bUyz4i9bb7Xed7"
     "-e4mdQu-Kike6ihOgmi7lX3vcGfsPZX8h_t6m_1ejLccW8aw1jpx6nt4znt7ktZ1ny6qWCwev7tpb7jw6nxdFfWm8-hA"
     "fsBlY-Edi7lHpav8dgp6-Z2daConX1u_b7nax7u-a1Xlx6dZPrtDyW1-jv6fk2smw8qd3gk6nsm6wi2ydDhzFzbg7vsY"
     "9ns-EyXz7pWd1x_a7bpEqtv6mp3aZ7XdHls9r_7rhY7bWr8odv6cbp1pi4hZc6fj5WZEWGWdl8rx6tAr-Ajff7_-9iau"
     "6yfs1hEfeh6eZd8hbCvjz7fdSdY2pk1jav6f-l8WtBqq4ekn7haHuxq7-mDvrz6irf8zNqhd7klr1ilq6yyd1alj7_z2"
     "Zx5jcd7uxGzqd7mY2zfb1cxl6-rf1oiEuzf6dnA"},
    {"Microsoft", "",
     "_Vxf2Wf1ZOzK-k1mZ1cRwhW8roZ8-lv7hj6uW3dlf1cYo6_rIheJxsq7rgCwxpEZx-7oXJdvHjexEmEpiq1iyl6wtAqv"
     "tNup8ufh8nqTnZc6rzUs_X7nok8yrAvz2tdj7hbaGtch8scm8lriNotY1ihd6s_Hhxo8hx4-aFnh_6plJt_4Zq2Yze7a"
     "tv7uyKyfX8jga7Zb3imJrtxMqmI-zh8gfn7Xo6Xmy7cmFZYe7-yEYoXG_pW8uteNWn7da3gmN_eo7mwq8otY7cXFzm8i"
     "x2mzy6_vLcxz6ztKzh5xzCpr1hlu7ra_7izq8Xhj7ipaFoXq1W2sTxYg7awJnWq6Zx4fhAipc7lYKic_7lrl8hqu7"},
    {"Panasonic", "",
     "ou3rQWv1iIjZDmbC_yFn-3wXq6fyt7thv8rZgd1yck1mr7eqa7wYCXWfNXev7dBi_m1ue-EYjiVYc5uffGgiSYuYt1ih"
     "p8ioChyxNik-NsXtFc-mFtoiNqenH_za7dYj7zhd8W-AthMXXbVay9geiOv9ndlGnzzM"},
    {"Sharp", "",
     "kd7bm1-ghe1nlrVYw2sxq8ierNdXd1W-Wd3jpeVyXdGZicOtqa9XvuNqmb8froNkxzd4"},
    {"AzureWave", "",
     "lj5qa3WFmDat5Zah1WXmF-slFxtg7ndm1ozzExpW1Zfl6YyGkxk7_Zt8w_Mch3jvw6dhe1joaFicu7ofoGsm1utGYmz7"
     "kfX7wpY1ynm7xZX7q-AmgOtoGoeg6dts7nYb9-gn7g_e1ezb7pqsF_WFzYk7peo7psb1onAqWg6ob-Gfk4jKfZ8hX3nq"
     "eVwzSwrh7XX9ca8gWxFZgrM-c9zae1auf7mbv8uod7XviOsZ9npr6xaZGmlVZbW8lj8qxi7mjf8ufn7dvRWbn7qpdGZu"
     "8yxi6-z2sfo7tgPZdrFmv5"},
    {"Murata", "",
     "jp3ph1em3We1ba4gdAqeFli4hxb7ylVk-kFzli7tqPje3r_r7buo7uwf8gfAZhw6-so9vv_EWeX1avlF-muOtxb7Xt1l"
     "p5pym8jykExmhHgkX8jk7_eg6f-p1hqWVpy4fty7tdq7-hg1r-m7Xcu7rd_7wlc7sZb8ciy8gtkGXxt6lc-8hKeEttk6"
     "ytBqghObi3mdvGgbrNWXbGkro6YY3ciAxwe1wgw6_k1bdl7YWj1Wxg8vo3dzhFra7ccX7oic1j-y6Xto7Wr5YrnOtnFr"
     "c6"},
    {"Quectel", "",
     "euc1l8qrErzh7xlqFnYgGpmp8-olErXZ8i_1affHgpCnoZFlyaFpz1fsi8yfn7t-yNgm4ex5zZIgsJhs6Yjt6Yi7kqx8"
     "ux1ixt7c-z6eka9bpp6nn4rk9melNzsRXrrGsm6-cb8XyXUbhBflTdv5vv3jwe7Xoj7zqUixu6bgw9pbc7Yjk8lg7gy-"
     "7qxi6cpe8osl1wru7ajs6_yVbxbFtePfleGWpvUrvZ1xuoEqeVfmd7Zv8_pHql2YaGYin6ixb1oozFrbh8"},
    {"Realtek", "",
     "kwavF"},
};
constexpr uint16_t kMakerCount = sizeof(kMakers) / sizeof(kMakers[0]);

}  // namespace
