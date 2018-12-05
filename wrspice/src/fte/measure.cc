
/*========================================================================*
 *                                                                        *
 *  Distributed by Whiteley Research Inc., Sunnyvale, California, USA     *
 *                       http://wrcad.com                                 *
 *  Copyright (C) 2017 Whiteley Research Inc., all rights reserved.       *
 *  Author: Stephen R. Whiteley, except as indicated.                     *
 *                                                                        *
 *  As fully as possible recognizing licensing terms and conditions       *
 *  imposed by earlier work from which this work was derived, if any,     *
 *  this work is released under the Apache License, Version 2.0 (the      *
 *  "License").  You may not use this file except in compliance with      *
 *  the License, and compliance with inherited licenses which are         *
 *  specified in a sub-header below this one if applicable.  A copy       *
 *  of the License is provided with this distribution, or you may         *
 *  obtain a copy of the License at                                       *
 *                                                                        *
 *        http://www.apache.org/licenses/LICENSE-2.0                      *
 *                                                                        *
 *  See the License for the specific language governing permissions       *
 *  and limitations under the License.                                    *
 *                                                                        *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,      *
 *   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES      *
 *   OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-        *
 *   INFRINGEMENT.  IN NO EVENT SHALL WHITELEY RESEARCH INCORPORATED      *
 *   OR STEPHEN R. WHITELEY BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER     *
 *   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,      *
 *   ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE       *
 *   USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                        *
 *========================================================================*
 *               XicTools Integrated Circuit Design System                *
 *                                                                        *
 * WRspice Circuit Simulation and Analysis Tool                           *
 *                                                                        *
 *========================================================================*
 $Id:$
 *========================================================================*/

#include "simulator.h"
#include "runop.h"
#include "parser.h"
#include "misc.h"
#include "datavec.h"
#include "output.h"
#include "cshell.h"
#include "device.h"
#include "rundesc.h"
#include "spnumber/hash.h"
#include "spnumber/spnumber.h"
#include "miscutil/lstring.h"

//
// Functions for the measurement post-processor.
//

//#define M_DEBUG

namespace {

    // Keywords
    const char *mkw_measure     = "measure";
    const char *mkw_trig        = "trig";
    const char *mkw_targ        = "targ";
    const char *mkw_at          = "at";
    const char *mkw_when        = "when";
    const char *mkw_val         = "val";
    const char *mkw_td          = "td";
    const char *mkw_cross       = "cross";
    const char *mkw_rise        = "rise";
    const char *mkw_fall        = "fall";
    const char *mkw_from        = "from";
    const char *mkw_to          = "to";
    const char *mkw_min         = "min";
    const char *mkw_max         = "max";
    const char *mkw_pp          = "pp";
    const char *mkw_avg         = "avg";
    const char *mkw_rms         = "rms";
    const char *mkw_pw          = "pw";
    const char *mkw_rt          = "rt";
    const char *mkw_find        = "find";
    const char *mkw_param       = "param";
    const char *mkw_goal        = "goal";
    const char *mkw_minval      = "minval";
    const char *mkw_weight      = "weight";
    const char *mkw_print       = "print";
    const char *mkw_print_terse = "print_terse";
    const char *mkw_stop        = "stop";
    const char *mkw_call        = "call";
}


void
sMfunc::print(sLstr &lstr)
{
    lstr.add_c(' ');
    if (f_type == Mmin)
        lstr.add(mkw_min);
    else if (f_type == Mmax)
        lstr.add(mkw_max);
    else if (f_type == Mpp)
        lstr.add(mkw_pp);
    else if (f_type == Mavg)
        lstr.add(mkw_avg);
    else if (f_type == Mrms)
        lstr.add(mkw_rms);
    else if (f_type == Mpw)
        lstr.add(mkw_pw);
    else if (f_type == Mrft)
        lstr.add(mkw_rt);
    else if (f_type == Mfind)
        lstr.add(mkw_find);
    lstr.add_c(' ');
    lstr.add(f_expr);
}
// End of sMfunc functions.



namespace {
    // Return true if name names an analysis, and return the index in
    // which.
    //
    bool get_anal(const char *name, int *which)
    {
        for (int i = 0; ; i++) {
            IFanalysis *a = IFanalysis::analysis(i);
            if (!a)
                break;
            if (lstring::cieq(a->name, name)) {
                *which = i;
                return (true);
            }
        }
        *which = -1;
        return (false);
    }


    // Return comma or space separated token, with '=' considered as a
    // token.  If kw is true, terminate tokens at '(', splitting forms
    // like "param(val)".  Otherwise the parentheses will be included
    // in the token.  The former is appropriate when a keyword is
    // expected, the latter if an expression is expected.
    //
    char *gtok(const char **s, bool kw = false)
    {
        if (s == 0 || *s == 0)
            return (0);
        while (isspace(**s) || **s == ',')
            (*s)++;
        if (!**s)
            return (0);
        char buf[BSIZE_SP];
        char *t = buf;
        if (**s == '=')
            *t++ = *(*s)++;
        else {
            // Treat (....) as a single token.  Expressions can be
            // delimited this way to ensure that they are parsed
            // correctly.  Single quotes were originally used for this
            // purpose, but single quoted exporessions are now
            // evaluated at circuit load time.
            //
            if (**s == '(') {
                (*s)++;
                int np = 1;
                while (**s && np) {
                    if (**s == '(')
                        np++;
                    else if (**s == ')')
                        np--;
                    if (np)
                        *t++ = *(*s)++;
                    else
                        (*s)++;
                }
            }
            else {
                // Here's the problem:  Hspice can apparently handle
                // forms like
                //   ... trig vin'expression'
                // where there is no space between the vin and the
                // single quote.  We translate this to
                //   ... trig vin(expression)
                // implying that we need to stop parsing the trig at '('.
                // However, this breaks forms like
                //   ... trig v(vin) ...
                // The fix is to
                // 1. Add a space ahead of the leading '(' when we
                //    translate.
                // 2. Recognize and perform the following translations
                //    here:
                //    v(x) -> x
                //    i(x) -> x#branch

                const char *p = *s;
                if (kw && p[1] == '(') {
                    char ch = isupper(*p) ? tolower(*p) : *p;
                    if (ch == 'v' || ch == 'i') {
                        p += 2;
                        while (isspace(*p))
                            p++;
                        while (*p && !isspace(*p) && *p != ',' && *p != ')' &&
                                *p != '(') {
                            *t++ = *p++;
                        }
                        while (*p && *p != ')')
                            p++;
                        if (ch == 'i')
                            t = lstring::stpcpy(t, "#branch");
                        if (*p)
                            p++;
                    }
                }
                if (p != *s)
                    *s = p;
                else {
                    while (**s && !isspace(**s) && **s != '=' && **s != ',' &&
                            (!kw || **s != '(')) {
                        *t++ = *(*s)++;
                    }
                }
            }
        }
        *t = 0;
        while (isspace(**s) || **s == ',')
            (*s)++;
        return (lstring::copy(buf));
    }


    // Return a double found after '=', set err if problems.
    //
    double gval(const char **s, bool *err)
    {
        char *tok = gtok(s);
        if (!tok) {
            *err = true;
            return (0.0);
        }
        if (*tok == '=') {
            delete [] tok;
            tok = gtok(s);
        }
        const char *t = tok;
        double *dd = SPnum.parse(&t, false);
        delete [] tok;
        if (!dd) {
            *err = true;
            return (0.0);
        }
        *err = false;
        return (*dd);
    }


    inline void listerr(char **errstr, const char *string)
    {
        if (!errstr)
            return;
        if (!*errstr) {
            if (string) {
                char buf[128];
                sprintf(buf, ".measure syntax error after \'%s\'.", string);
                *errstr = lstring::copy(buf);
            }
            else
                *errstr = lstring::copy(".measure syntax error.");
        }
    }
}


namespace {
    bool is_kw(const char *s)
    {
        if (lstring::cieq(s, mkw_measure)) return (true);
        if (lstring::cieq(s, mkw_trig)) return (true);
        if (lstring::cieq(s, mkw_targ)) return (true);
        if (lstring::cieq(s, mkw_at)) return (true);
        if (lstring::cieq(s, mkw_when)) return (true);
        if (lstring::cieq(s, mkw_val)) return (true);
        if (lstring::cieq(s, mkw_td)) return (true);
        if (lstring::cieq(s, mkw_cross)) return (true);
        if (lstring::cieq(s, mkw_rise)) return (true);
        if (lstring::cieq(s, mkw_fall)) return (true);
        if (lstring::cieq(s, mkw_from)) return (true);
        if (lstring::cieq(s, mkw_to)) return (true);
        if (lstring::cieq(s, mkw_min)) return (true);
        if (lstring::cieq(s, mkw_max)) return (true);
        if (lstring::cieq(s, mkw_pp)) return (true);
        if (lstring::cieq(s, mkw_avg)) return (true);
        if (lstring::cieq(s, mkw_rms)) return (true);
        if (lstring::cieq(s, mkw_pw)) return (true);
        if (lstring::cieq(s, mkw_rt)) return (true);
        if (lstring::cieq(s, mkw_find)) return (true);
        if (lstring::cieq(s, mkw_param)) return (true);
        if (lstring::cieq(s, mkw_goal)) return (true);
        if (lstring::cieq(s, mkw_minval)) return (true);
        if (lstring::cieq(s, mkw_weight)) return (true);
        if (lstring::cieq(s, mkw_print)) return (true);
        if (lstring::cieq(s, mkw_print_terse)) return (true);
        if (lstring::cieq(s, mkw_stop)) return (true);
        if (lstring::cieq(s, mkw_call)) return (true);
        return (false);
    }
}


sTpoint::~sTpoint()
{
    delete [] t_when_expr1;
    delete [] t_when_expr2;
    delete t_tree1;
    delete t_tree2;
    delete [] t_mname;
}


// The general form of the definition string is
//   [when/at] expr[val][=][expr] [td=delay] [cross=crosses] [rise=rises]
//     [fall=falls]
// The initial keyword (which may be missing if unambiguous) is one of
// "at" or "when".  These are equivalent.  One or two expressions follow,
// with optional '=' or 'val=' ahead of the second expression.  the second
// expression can be missing.
//
// TPexp2:  expr1 and expr2 are both given, then the point is when
//   expr==expr2, and the td,cross,rise,fall keywords apply.  The risis,
//   falls, crosses are integers.  The delay is a numeric value, or the
//   name of another measure.  The trigger is the matching
//   rise/fall/cross found after the delay.
//
// If expr2 is not given, then expr1 is one of:
//
// TPnum:  (numeric value) Gives the point directly, no other keywords
//   apply.
//
// TPmref:  (measure name) Point where given measure completes,
//   numeric td applies, triggers at the referenced measure time plus
//   delay.
//
// TPexpr1:  (expression) Point where expression is boolen true, td
//   applies, can be numeric or measure name, trigers when expr is true
//   after delay.
//
int
sTpoint::parse(const char **pstr, char **errstr)
{
    const char *s = *pstr;
    const char *sbk = s;
    char *tok = gtok(&s, true);
    if (!tok) {
        if (errstr && !*errstr)
            lstring::copy("unexpected empty line passed to parser.");
        return (E_SYNTAX);
    }
    if (lstring::cieq(tok, mkw_at) || lstring::cieq(tok, mkw_when)) {
        delete [] tok;
        tok = gtok(&s);
    }
    else {
        delete [] tok;
        s = sbk;
        tok = gtok(&s);
    }
    if (!tok) {
        if (errstr && !*errstr)
            lstring::copy("premature end of line.");
        return (E_SYNTAX);
    }
    t_when_expr1 = tok;

    sbk = s;
    tok = gtok(&s);
    if (tok) {
        if (lstring::cieq(tok, mkw_val)) {
            delete [] tok;
            sbk = s;
            tok = gtok(&s);
        }
        if (tok) {
            if (*tok == '=') {
                delete [] tok;
                sbk = s;
                tok = gtok(&s);
            }
            if (tok) {
                if (is_kw(tok)) {
                    delete [] tok;
                    s = sbk;
                }
                else
                    t_when_expr2 = tok;
            }
        }
    }
    if (t_when_expr2)
        t_type = TPexp2;
    else {
        const char *t = t_when_expr1;
        double *dd = SPnum.parse(&t, false, true);
        if (dd && !*t)
            t_type = TPnum;
        else
            t_type = TPexp1;
    }

    const char *last = s;
    while ((tok = gtok(&s, true)) != 0) {
        if (lstring::cieq(tok, mkw_td)) {
            delete [] tok;
            tok = gtok(&s);
            if (!tok) {
                listerr(errstr, mkw_td);
                break;
            }
            if (*tok == '=') {
                delete [] tok;
                tok = gtok(&s);
            }
            if (!tok) {
                listerr(errstr, mkw_td);
                break;
            }
            const char *t = tok;
            double *dd = SPnum.parse(&t, false, true);
            if (dd && !*t) {
                t_delay_given = *dd;
                delete [] tok;
            }
            else
                t_mname = tok;
            last = s;
            continue;
        }
        if (t_type == TPexp2) {
            if (lstring::cieq(tok, mkw_cross)) {
                delete [] tok;
                bool err;
                t_crosses = (int)gval(&s, &err);
                if (err) {
                    listerr(errstr, mkw_cross);
                    break;
                }
                last = s;
                continue;
            }
            if (lstring::cieq(tok, mkw_rise)) {
                delete [] tok;
                bool err;
                t_rises = (int)gval(&s, &err);
                if (err) {
                    listerr(errstr, mkw_rise);
                    return (E_SYNTAX);
                }
                last = s;
                continue;
            }
            if (lstring::cieq(tok, mkw_fall)) {
                delete [] tok;
                bool err;
                t_falls = (int)gval(&s, &err);
                if (err) {
                    listerr(errstr, mkw_fall);
                    return (E_SYNTAX);
                }
                last = s;
                continue;
            }
        }
        s = last;
        break;
    }

    t_active = true;
    *pstr = s;
#ifdef M_DEBUG
    sLstr lstr;
    print(lstr);
    printf("sTpoint:  %s\n", lstr.string());
#endif
    return (OK);
}


void
sTpoint::print(sLstr &lstr)
{
    if (!t_active)
        return;
    if (t_type == TPnum) {
        lstr.add_c(' ');
        lstr.add(mkw_at);
        lstr.add_c(' ');
        lstr.add_g(t_delay_given);
    }
    else if (t_type == TPmref) {
        lstr.add_c(' ');
        lstr.add(t_when_expr1);
        if (t_delay_given > 0.0) {
            lstr.add_c(' ');
            lstr.add(mkw_td);
            lstr.add_c('=');
            lstr.add_g(t_delay_given);
        }
    }
    else if (t_type == TPexp1) {
        lstr.add_c(' ');
        lstr.add(t_when_expr1);
        if (t_delay_given > 0.0 || t_mname) {
            lstr.add_c(' ');
            lstr.add(mkw_td);
            lstr.add_c('=');
            if (t_mname)
                lstr.add(t_mname);
            else
                lstr.add_g(t_delay_given);
        }
    }
    else if (t_type == TPexp2) {
        lstr.add_c(' ');
        lstr.add(mkw_when);
        lstr.add_c(' ');
        lstr.add(t_when_expr1);
        lstr.add_c('=');
        lstr.add(t_when_expr2);
        if (t_delay_given > 0.0 || t_mname) {
            lstr.add_c(' ');
            lstr.add(mkw_td);
            lstr.add_c('=');
            if (t_mname)
                lstr.add(t_mname);
            else
                lstr.add_g(t_delay_given);
        }
        if (t_rises) {
            lstr.add_c(' ');
            lstr.add(mkw_rise);
            lstr.add_c('=');
            lstr.add_u(t_rises);
        }
        if (t_falls) {
            lstr.add_c(' ');
            lstr.add(mkw_fall);
            lstr.add_c('=');
            lstr.add_u(t_falls);
        }
        if (t_crosses) {
            lstr.add_c(' ');
            lstr.add(mkw_cross);
            lstr.add_c('=');
            lstr.add_u(t_crosses);
        }
    }
}


bool
sTpoint::setup_delay(sFtCirc *circuit, bool *err)
{
    if (!t_active)
        return (true);
    if (!t_init) {
        t_delay = t_delay_given;
        sRunopMeas *m = 0;
        if (t_type == TPnum) {
#ifdef M_DEBUG
            printf("setup_delay:  numeric\n");
#endif
            t_init = true;
            return (true);
        }
        if (t_type == TPexp1) {
            m = sRunopMeas::find(circuit->measures(), t_when_expr1);
            if (m) {
                t_type = TPmref;
#ifdef M_DEBUG
                printf("setup_delay: found expr1 is measure %s\n",
                    t_when_expr1);
#endif
            }
        }
        if (t_type == TPmref) {
            if (!m)
                m = sRunopMeas::find(circuit->measures(), t_when_expr1);
            if (!m) {
                if (err)
                    *err = true;
                return (false);
            }
            if (!m->measure_done())
                return (false);
            if (m->end().t_found_flag)
                t_delay += m->end().t_found;
            else
                t_delay += m->start().t_found;
            t_init = true;
#ifdef M_DEBUG
            printf("setup_delay:  expr measure reference %s, delay %g\n",
                t_when_expr1, t_delay);
#endif
            return (true);
        }
        if (t_type == TPexp1 || t_type == TPexp2) {
            if (t_mname) {
                m = sRunopMeas::find(circuit->measures(), t_mname);
                if (!m) {
                    if (err)
                        *err = true;
                    return (false);
                }
                if (!m->measure_done())
                    return (false);
                if (m->end().t_found_flag)
                    t_delay += m->end().t_found;
                else
                    t_delay += m->start().t_found;
#ifdef M_DEBUG
                printf("setup_delay:  td measure reference %s, delay %g\n",
                    t_mname, t_delay);
#endif
            }
            if (t_type == TPexp2) {
                // If none of these are set, assume the first crossing.
                if (t_crosses == 0 && t_rises == 0 && t_falls == 0)
                    t_crosses = 1;
            }
#ifdef M_DEBUG
            printf("setup_delay:  %s\n", t_type == TPexp1 ?
                "expr1" : "expr2");
#endif
            t_init = true;
        }
    }
    return (true);
}


namespace {
    // Return a non-negative index if the scale covers the trigger point
    // of val.  Do some fudging to avoid not triggering at the expected
    // time point due to numerical error.
    //
    int chk_trig(double val, sDataVec *xs)
    {
        int len = xs->length();
        if (len > 1) {
            if (xs->realval(1) > xs->realval(0)) {
                // Assume monotonically increasing.
                if (xs->realval(0) >= val)
                    return (0);
                for (int i = 1; i < len; i++) {
                    double dx = (xs->realval(i) - xs->realval(i-1)) * 1e-3;
                    if (xs->realval(i) >= val - dx) {
                        if (fabs(xs->realval(i-1) - val) <
                                fabs(xs->realval(i) - val))
                            i--;
                        return (i);
                    }
                }
            }
            else {
                // Assume monotonically decreasing.
                if (xs->realval(0) <= val)
                    return (0);
                for (int i = 1; i < len; i++) {
                    double dx = (xs->realval(i) - xs->realval(i-1)) * 1e-3;
                    if (xs->realval(i) <= val - dx) {
                        if (fabs(xs->realval(i-1) - val) <
                                fabs(xs->realval(i) - val))
                            i--;
                        return (i);
                    }
                }
            }
        }
        else if (len == 1) {
            if (xs->realval(0) == val)
                return (0);
        }
        return (-1);
    }


    // Return a data vector structure representing the expression
    // evaluation.
    //
    sDataVec *evaluate(const char *str)
    {
        if (!str)
            return (0);
        const char *s = str;
        pnode *pn = Sp.GetPnode(&s, true);
        if (!pn)
            return (0);
        sDataVec *dv = Sp.Evaluate(pn);
        delete pn;
        return (dv);
    }


    // Return the indexed value, or the last value if index is too big.
    //
    inline double value(sDataVec *dv, int i)
    {
        if (i < dv->length())
            return (dv->realval(i));
        return (dv->realval(dv->length() - 1));
    }
}


bool
sTpoint::check_found(sFtCirc *circuit, bool *err, bool end)
{
    if (!t_active || t_found_flag)
        return (true);

    sDataVec *xs = circuit->runplot()->scale();
    int ix = chk_trig(t_delay, xs);
    if (ix < 0)
        return (false);

    double fval = t_delay;
    if (t_type == TPnum)
        fval = xs->realval(ix);
    else if (t_type == TPmref) {
    }
    else if (t_type == TPexp1) {
        sDataVec *dvl = eval1();
        if (!dvl) {
            if (err)
                *err = true;
            return (false);
        }
//XXX finish me
    }
    else if (t_type == TPexp2) {
        sDataVec *dvl = eval1();
        sDataVec *dvr = eval2();
        if (!dvl || !dvr) {
            if (err)
                *err = true;
            return (false);
        }
        int r = 0, f = 0, c = 0;
        bool foundit = false;
        for ( ; ix < dvl->length(); ix++) {
            if (ix && value(dvl, ix-1) <= value(dvr, ix-1) &&
                    value(dvr, ix) < value(dvl, ix)) {
                r++;
                c++;
            }
            else if (ix && value(dvl, ix-1) > value(dvr, ix-1) &&
                    value(dvr, ix) >= value(dvl, ix)) {
                f++;
                c++;
            }
            else
                continue;
            if (r >= t_rises && f >= t_falls && c >= t_crosses) {
                double d = value(dvr, ix) - value(dvr, ix-1) -
                    (value(dvl, ix) - value(dvl, ix-1));
                if (d != 0.0) {
                    fval = xs->realval(ix-1) +
                        (xs->realval(ix) - xs->realval(ix-1))*
                        (value(dvl, ix-1) - value(dvr, ix-1))/d;
                }
                else
                    fval = xs->realval(ix);
                foundit = true;
                if (end)
                    ix--;
                break;
            }
        }
        if (!foundit)
            return (false);
    }

    t_indx = ix;
    t_found = fval;
    t_found_flag = true;
    return (true);
}


sDataVec *
sTpoint::eval1()
{
    if (!t_when_expr1)
        return (0);
    if (!t_tree1) {
        const char *s = t_when_expr1;
        t_tree1 = Sp.GetPnode(&s, true);
        if (t_tree1)
            t_tree1->copyvecs();
    }
    if (t_tree1)
        return (Sp.Evaluate(t_tree1));
    return (0);
}


sDataVec *
sTpoint::eval2()
{
    if (!t_when_expr2)
        return (0);
    if (!t_tree2) {
        const char *s = t_when_expr2;
        t_tree2 = Sp.GetPnode(&s, true);
        if (t_tree2)
            t_tree2->copyvecs();
    }
    if (t_tree2)
        return (Sp.Evaluate(t_tree2));
    return (0);
}
// End of sTpoint functions.


void
sRunopMeas::print(char **pstr)
{
    sLstr lstr;
    lstr.add(mkw_measure);
    if (ro_analysis >= 0) {
        IFanalysis *a = IFanalysis::analysis(ro_analysis);
        if (a) {
            lstr.add_c(' ');
            lstr.add(a->name);
        }
    }
    if (ro_result) {
        lstr.add_c(' ');
        lstr.add(ro_result);
    }
    ro_start.print(lstr);
    ro_end.print(lstr);

    if (ro_end.active()) {
        for (sMfunc *m = ro_funcs; m; m = m->next())
            m->print(lstr);
    }
    else {
        for (sMfunc *m = ro_finds; m; m = m->next())
            m->print(lstr);
    }

    lstr.add_c('\n');
    if (pstr)
        *pstr = lstr.string_trim();
    else
        TTY.send(lstr.string());

    if (ro_print_flag == 2) {
        lstr.add_c(' ');
        lstr.add(mkw_print);
    }
    else if (ro_print_flag == 1) {
        lstr.add_c(' ');
        lstr.add(mkw_print_terse);
    }
    if (ro_call_flag) {
        lstr.add_c(' ');
        lstr.add(mkw_call);
        if (ro_call) {
            lstr.add_c(' ');
            lstr.add(ro_call);
        }   
    }
    if (ro_stop_flag) {
        lstr.add_c(' ');
        lstr.add(mkw_stop);
    }
}


void
sRunopMeas::destroy()
{
    delete this;
}


// Parse the string and set up the structure appropriately.
//
bool
sRunopMeas::parse(const char *str, char **errstr)
{
    bool err = false;
    bool gotanal = false;
    bool in_call = false;
    ro_analysis = -1;
    const char *s = str;
    char *tok;
    while ((tok = gtok(&s, true)) != 0) {
        if (!gotanal) {
            if (get_anal(tok, &ro_analysis)) {
                gotanal = true;
                delete [] tok;
                ro_result = gtok(&s);
                if (!ro_result) {
                    err = true;
                    listerr(errstr, 0);
                    break;
                }
                in_call = false;
                continue;
            }
        }
        if (lstring::cieq(tok, mkw_trig)) {
            delete [] tok;
            int ret = ro_start.parse(&s, errstr);
            if (ret != OK) {
                err = true;
                break;
            }
            in_call = false;
            continue;
        }
        if (lstring::cieq(tok, mkw_targ)) {
            delete [] tok;
            int ret = ro_end.parse(&s, errstr);
            if (ret != OK) {
                err = true;
                break;
            }
            in_call = false;
            continue;
        }
        if (lstring::cieq(tok, mkw_from)) {
            delete [] tok;
            int ret = ro_start.parse(&s, errstr);
            if (ret != OK) {
                err = true;
                break;
            }
            in_call = false;
            continue;
        }
        if (lstring::cieq(tok, mkw_to)) {
            delete [] tok;
            int ret = ro_start.parse(&s, errstr);
            if (ret != OK) {
                err = true;
                break;
            }
            in_call = false;
            continue;
        }
        if (lstring::cieq(tok, mkw_when)) {
            delete [] tok;
            int ret = ro_start.parse(&s, errstr);
            if (ret != OK) {
                err = true;
                break;
            }
            in_call = false;
            continue;
        }
        if (lstring::cieq(tok, mkw_at)) {
            delete [] tok;
            int ret = ro_start.parse(&s, errstr);
            if (ret != OK) {
                err = true;
                break;
            }
            in_call = false;
            continue;
        }
        if (lstring::cieq(tok, mkw_min)) {
            delete [] tok;
            addMeas(Mmin, gtok(&s));
            in_call = false;
            continue;
        }
        if (lstring::cieq(tok, mkw_max)) {
            delete [] tok;
            addMeas(Mmax, gtok(&s));
            in_call = false;
            continue;
        }
        if (lstring::cieq(tok, mkw_pp)) {
            delete [] tok;
            addMeas(Mpp, gtok(&s));
            in_call = false;
            continue;
        }
        if (lstring::cieq(tok, mkw_avg)) {
            delete [] tok;
            addMeas(Mavg, gtok(&s));
            in_call = false;
            continue;
        }
        if (lstring::cieq(tok, mkw_rms)) {
            delete [] tok;
            addMeas(Mrms, gtok(&s));
            in_call = false;
            continue;
        }
        if (lstring::cieq(tok, mkw_pw)) {
            delete [] tok;
            addMeas(Mpw, gtok(&s));
            in_call = false;
            continue;
        }
        if (lstring::cieq(tok, mkw_rt)) {
            delete [] tok;
            addMeas(Mrft, gtok(&s));
            in_call = false;
            continue;
        }
        if (lstring::cieq(tok, mkw_find)) {
            delete [] tok;
            addMeas(Mfind, gtok(&s));
            in_call = false;
            continue;
        }
        if (lstring::cieq(tok, mkw_param)) {
            delete [] tok;
            ro_expr2 = gtok(&s);
            if (ro_expr2 && *ro_expr2 == '=') {
                delete [] ro_expr2;
                ro_expr2 = gtok(&s);
            }
            if (!ro_expr2) {
                err = true;
                listerr(errstr, mkw_param);
                break;
            }
            in_call = false;
            continue;
        }
        if (lstring::cieq(tok, mkw_goal)) {
            delete [] tok;
            gval(&s, &err);
            if (err) {
                listerr(errstr, mkw_goal);
                break;
            }
            in_call = false;
            continue;
        }
        if (lstring::cieq(tok, mkw_minval)) {
            delete [] tok;
            gval(&s, &err);
            if (err) {
                listerr(errstr, mkw_minval);
                break;
            }
            in_call = false;
            continue;
        }
        if (lstring::cieq(tok, mkw_weight)) {
            delete [] tok;
            gval(&s, &err);
            if (err) {
                listerr(errstr, mkw_weight);
                break;
            }
            in_call = false;
            continue;
        }
        if (lstring::cieq(tok, mkw_print)) {
            delete [] tok;
            ro_print_flag = 2;
            in_call = false;
            continue;
        }
        if (lstring::cieq(tok, mkw_print_terse)) {
            ro_print_flag = 1;
            in_call = false;
            continue;
        }
        if (lstring::cieq(tok, mkw_stop)) {
            delete [] tok;
            ro_stop_flag = true;
            in_call = false;
            continue;
        }

        // This should follow all other keywords.
        if (lstring::cieq(tok, mkw_call)) {
            delete [] tok;
            ro_call_flag = true;
            // Ugly logic, the script name is optional, take the next
            // token as the script name if it is not a keyword.
            in_call = true;
            continue;
        }
        if (in_call) {
            ro_call = tok;
            in_call = false;
            continue;
        }

        if (errstr && !*errstr) {
            char buf[128];
            sprintf(buf, ".measure syntax error, unknown token \'%s\'.", tok);
            *errstr = lstring::copy(buf);
            err = true;
            delete [] tok;
        }
    }
    if (err)
        ro_measure_skip = true;
    return (true);
}


// Reset the measurement.
//
void
sRunopMeas::reset(sPlot *pl)
{
    if (pl) {
        // If the result vector is found in pl, delete it after copying
        // to a history list in pl.
        sDataVec *v = pl->get_perm_vec(ro_result);
        if (v) {
            char buf[128];
            pl->remove_perm_vec(ro_result);
            sprintf(buf, "%s_scale", ro_result);
            sDataVec *vs = pl->get_perm_vec(buf);
            if (vs) {
                pl->remove_perm_vec(buf);
                sprintf(buf, "%s_hist", ro_result);
                sDataVec *vh = pl->get_perm_vec(buf);
                if (!vh) {
                    vh = new sDataVec(*v->units());
                    vh->set_name(buf);
                    vh->set_flags(0);
                    sPlot *px = OP.curPlot();
                    OP.setCurPlot(pl);
                    vh->newperm();
                    OP.setCurPlot(px);
                }
                sDataVec *vhs = vh->scale();
                if (!vhs) {
                    sprintf(buf, "%s_hist_scale", ro_result);
                    vhs = new sDataVec(*vs->units());
                    vhs->set_name(buf);
                    vhs->set_flags(0);
                    sPlot *px = OP.curPlot();
                    OP.setCurPlot(pl);
                    vhs->newperm();
                    OP.setCurPlot(px);
                    vh->set_scale(vhs);
                }

                int len = vh->length();
                int nlen = len + v->length();
                double *old = vh->realvec();
                vh->set_realvec(new double[nlen]);
                vh->set_length(nlen);
                vh->set_allocated(nlen);
                int i;
                for (i = 0; i < len; i++)
                    vh->set_realval(i, old[i]);
                for (i = len; i < nlen; i++)
                    vh->set_realval(i, v->realval(i-len));
                delete [] old;

                len = vhs->length();
                nlen = len + vs->length();
                old = vhs->realvec();
                vhs->set_realvec(new double[nlen]);
                vhs->set_length(nlen);
                vhs->set_allocated(nlen);
                for (i = 0; i < len; i++)
                    vhs->set_realval(i, old[i]);
                for (i = len; i < nlen; i++)
                    vhs->set_realval(i, vs->realval(i-len));
                delete [] old;
                delete vs;
            }
            delete v;
        }
    }

    ro_start.reset();
    ro_end.reset();

    ro_found_rises      = 0;
    ro_found_falls      = 0;
    ro_found_crosses    = 0;
    ro_measure_done     = false;
    ro_measure_error    = false;
    ro_stop_flag        = false;
    ro_end_flag         = false;
}


// Check if the measurement is triggered, and if so perform the
// measurement.  This is called periodically as the analysis
// progresses, or can be called after the analysis is complete.  True
// is returned if the measurement was performed.
//
bool
sRunopMeas::check(sRunDesc *run)
{
    if (!run || !run->circuit())
        return (true);
    sFtCirc *circuit = run->circuit();
    if (ro_measure_done || ro_measure_error || ro_measure_skip)
        return (true);
    sRunopMeas *measures = circuit->measures();
    if (!measures)
       return (true);  // "can't happen"
    if (ro_expr2) {
        for (sRunopMeas *m = measures; m; m = m->next()) {
            if (ro_analysis != m->ro_analysis)
                continue;
            if (m == this)
                continue;
            if (m->ro_expr2)
                continue;
            if (!m->ro_measure_done && !m->ro_measure_error)
                return (false);
        }
        // All non-param measurements done.
    }
    else {
        if (!ro_start.setup_delay(circuit, &ro_measure_error))
            return (false);
        if (!ro_end.setup_delay(circuit, &ro_measure_error))
            return (false);
        if (!ro_start.check_found(circuit, &ro_measure_error, false))
            return (false);
        if (!ro_end.check_found(circuit, &ro_measure_error, true))
            return (false);
    }
    ro_cktptr = circuit;

    sDataVec *dv;
    int cnt;
    if (!measure(&dv, &cnt))
        return (false);
    if (!update_plot(dv, cnt))
        return (false);

    // call function
    ROret ret = call(run);
    if (ret == RO_PAUSE)
        ro_stop_flag = true;
    if (ret == RO_ENDIT)
        ro_end_flag = true;

    return (true);
}


// Do the measurement, call after successfully identifying the
// measure interval.
//
bool
sRunopMeas::measure(sDataVec **dvp, int *cntp)
{
    if (dvp)
        *dvp = 0;
    if (cntp)
        *cntp = 0;
    if (!ro_cktptr)
        return (false);
    sDataVec *xs = ro_cktptr->runplot()->scale();
    sDataVec *dv0 = 0;
    int count = 0;
    if (ro_start.found_flag() && ro_end.found_flag()) {
        for (sMfunc *ff = ro_funcs; ff; ff = ff->next(), count++) {
            sDataVec *dv = evaluate(ff->expr());
            if (dv && ((dv->length() > ro_start.indx() &&
                    dv->length() > ro_end.indx()) || dv->length() == 1)) {
                if (!dv0)
                    dv0 = dv;
                if (ff->type() == Mmin) {
                    if (dv->length() == 1)
                        ff->set_val(dv->realval(0));
                    else {
                        double mn = dv->realval(ro_start.indx());
                        for (int i = ro_start.indx()+1; i <= ro_end.indx();
                                i++) {
                            if (dv->realval(i) < mn)
                                mn = dv->realval(i);
                        }
                        double d;
                        d = endval(dv, xs, false);
                        if (d < mn)
                            mn = d;
                        d = endval(dv, xs, true);
                        if (d < mn)
                            mn = d;
                        ff->set_val(mn);
                    }
                }
                else if (ff->type() == Mmax) {
                    if (dv->length() == 1)
                        ff->set_val(dv->realval(0));
                    else {
                        double mx = dv->realval(ro_start.indx());
                        for (int i = ro_start.indx()+1; i <= ro_end.indx();
                                i++) {
                            if (dv->realval(i) > mx)
                                mx = dv->realval(i);
                        }
                        double d;
                        d = endval(dv, xs, false);
                        if (d > mx)
                            mx = d;
                        d = endval(dv, xs, true);
                        if (d > mx)
                            mx = d;
                        ff->set_val(mx);
                    }
                }
                else if (ff->type() == Mpp) {
                    if (dv->length() == 1)
                        ff->set_val(0.0);
                    else {
                        double mn = dv->realval(ro_start.indx());
                        double mx = mn;
                        for (int i = ro_start.indx()+1; i <= ro_end.indx();
                                i++) {
                            if (dv->realval(i) < mn)
                                mn = dv->realval(i);
                            else if (dv->realval(i) > mx)
                                mx = dv->realval(i);
                        }
                        double d;
                        d = endval(dv, xs, false);
                        if (d > mx)
                            mx = d;
                        if (d < mn)
                            mn = d;
                        d = endval(dv, xs, true);
                        if (d > mx)
                            mx = d;
                        if (d < mn)
                            mn = d;
                        ff->set_val(mx - mn);
                    }
                }
                else if (ff->type() == Mavg) {
                    if (dv->length() == 1)
                        ff->set_val(dv->realval(0));
                    else {
                        sDataVec *txs = ro_cktptr->runplot()->scale();
                        ff->set_val(findavg(dv, txs));
                    }
                }
                else if (ff->type() == Mrms) {
                    if (dv->length() == 1)
                        ff->set_val(dv->realval(0));
                    else {
                        sDataVec *txs = ro_cktptr->runplot()->scale();
                        ff->set_val(findrms(dv, txs));
                    }
                }
                else if (ff->type() == Mpw) {
                    if (dv->length() == 1)
                        ff->set_val(0);
                    else {
                        sDataVec *txs = ro_cktptr->runplot()->scale();
                        ff->set_val(findpw(dv, txs));
                    }
                }
                else if (ff->type() == Mrft) {
                    if (dv->length() == 1)
                        ff->set_val(0);
                    else {
                        sDataVec *txs = ro_cktptr->runplot()->scale();
                        ff->set_val(findrft(dv, txs));
                    }
                }
            }
            else {
                ff->set_error(true);
                ff->set_val(0);
            }
        }
    }
    else if (ro_start.found_flag()) {
        for (sMfunc *ff = ro_finds; ff; ff = ff->next(), count++) {
            sDataVec *dv = evaluate(ff->expr());
            if (dv) {
                ff->set_val(endval(dv, xs, false));
                if (!dv0)
                    dv0 = dv;
            }
            else {
                ff->set_error(true);
                ff->set_val(0.0);
            }
        }
    }
    else if (ro_expr2) {
        dv0 = evaluate(ro_expr2);
        if (!dv0) {
            ro_measure_error = true;
            return (false);
        }
    }
    else
        return (false);

    if (dvp)
        *dvp = dv0;
    if (cntp)
        *cntp = count;
    return (true);
}


// Add a vector containing the results to the plot.
//
bool
sRunopMeas::update_plot(sDataVec *dv0, int count)
{
    if (!ro_cktptr)
        return (false);
    if (ro_cktptr->runplot()) {

        // create the output vector and scale
        sPlot *pl = ro_cktptr->runplot();
        sDataVec *xs = pl->scale();
        sDataVec *nv = 0;
        if (ro_end.found_flag()) {
            // units test
            int uv = 0, us = 0;
            for (sMfunc *ff = ro_funcs; ff; ff = ff->next()) {
                if (ff->type() == Mpw || ff->type() == Mrft)
                    us++;
                else
                    uv++;
            }
            if (us) {
                if (!uv)
                    nv = new sDataVec(*xs->units());
                else
                    nv = new sDataVec();
            }
        }
        if (!nv) {
            if (dv0)
                nv = new sDataVec(*dv0->units());
            else {
                sUnits u;
                nv = new sDataVec(u);
            }
        }
        nv->set_name(ro_result);
        nv->set_flags(0);
        sPlot *px = OP.curPlot();
        OP.setCurPlot(pl);
        nv->newperm();
        OP.setCurPlot(px);
        char scname[128];
        sprintf(scname, "%s_scale", ro_result);
        sDataVec *ns = new sDataVec(*xs->units());
        ns->set_name(scname);
        ns->set_flags(0);
        px = OP.curPlot();
        OP.setCurPlot(pl);
        ns->newperm();
        OP.setCurPlot(px);
        nv->set_scale(ns);

        if (ro_expr2) {
            nv->set_length(1);
            nv->set_allocated(1);
            nv->set_realvec(new double[1]);
            if (dv0)
                nv->set_realval(0, dv0->realval(0));
            ns->set_length(1);
            ns->set_allocated(1);
            ns->set_realvec(new double[1]);
            ns->set_realval(0, xs ? xs->realval(xs->length()-1) : 0.0);
            if (!dv0) {
                // No measurement, the named result is the time value.
                nv->set_realval(0, ns->realval(0));
            }
        }
        else {
            if (count == 0) {
                count = 1;
                if (ro_end.found_flag())
                    count++;
                nv->set_realvec(new double[count]);
                // No measurement, the named result is the time value.
                nv->set_realval(0, ro_start.found());
                if (ro_end.found_flag())
                    nv->set_realval(1, ro_end.found());
            }
            else
                nv->set_realvec(new double[count]);
            nv->set_length(count);
            nv->set_allocated(count);
            count = 0;
            if (ro_end.found_flag()) {
                for (sMfunc *ff = ro_funcs; ff; ff = ff->next(), count++)
                    nv->set_realval(count, ff->val());
            }
            else {
                for (sMfunc *ff = ro_finds; ff; ff = ff->next(), count++)
                    nv->set_realval(count, ff->val());
            }
            count = 1;
            if (ro_end.found_flag())
                count++;
            ns->set_realvec(new double[count]);
            ns->set_length(count);
            ns->set_allocated(count);
            ns->set_realval(0, ro_start.found());
            if (ro_end.found_flag())
                ns->set_realval(1, ro_end.found());
        }
    }

    ro_measure_done = true;
    if (ro_print_flag && !Sp.GetFlag(FT_SERVERMODE)) {
        char *s = print();
        TTY.printf_force("%s", s);
        delete [] s;
    }
    return (true);
}


// Call the pose-measurement callback, if on is set up.
//
ROret
sRunopMeas::call(sRunDesc *run)
{
    // Call the callback, if any.  This can override the stop.
    if (ro_call_flag) {
        if (ro_call) {
            // Call the named script or codeblock.
            wordlist wl;
            wl.wl_word = lstring::copy(ro_call);
            Sp.ExecCmds(&wl);
            delete [] wl.wl_word;
            wl.wl_word = 0;
            if (CP.ReturnVal() == CB_PAUSE)
                return (RO_PAUSE);
            if (CP.ReturnVal() == CB_ENDIT)
                return (RO_ENDIT);
        }
        else if (run->check()) {
            // Run the "controls" bound codeblock.  We stop
            // only if the checkFAIL vector is not
            // set.
//XXX don't use checkFAIL here

            run->check()->evaluate();
            if (!run->check()->failed())
                return (RO_OK);
        }
        else {
            sFtCirc *circ = run->circuit();
            if (circ) {
                circ->controlBlk().exec(true);
                if (CP.ReturnVal() == CB_PAUSE)
                    return (RO_PAUSE);
                if (CP.ReturnVal() == CB_ENDIT)
                    return (RO_ENDIT);
            }
        }
    }
    return (RO_OK);
}


// Return a string containing text of measurement result.
//
char *
sRunopMeas::print()
{
    if (!ro_measure_done)
        return (0);
    sLstr lstr;
    char buf[BSIZE_SP];
    if (ro_print_flag > 1) {
        sprintf(buf, "measure: %s\n", ro_result);
        lstr.add(buf);
    }
    if (ro_start.found_flag() && ro_end.found_flag()) {
        if (ro_print_flag > 1) {
            if (ro_cktptr && ro_cktptr->runplot() &&
                    ro_cktptr->runplot()->scale()) {
                const char *zz = ro_cktptr->runplot()->scale()->name();
                if (zz && *zz) {
                    sprintf(buf, " %s\n", zz);
                    lstr.add(buf);
                }
            }
            sprintf(buf, "    start: %-16g end: %-16g delta: %g\n",
                ro_start.found(), ro_end.found(),
                ro_end.found() - ro_start.found());
            lstr.add(buf);
        }
        for (sMfunc *ff = ro_funcs; ff; ff = ff->next()) {
            if (ro_print_flag > 1) {
                sprintf(buf, " %s\n", ff->expr());
                lstr.add(buf);
            }
            const char *str = "???";
            if (ff->type() == Mmin)
                str = mkw_min;
            else if (ff->type() == Mmax)
                str = mkw_max;
            else if (ff->type() == Mpp)
                str = mkw_pp;
            else if (ff->type() == Mavg)
                str = mkw_avg;
            else if (ff->type() == Mrms)
                str = mkw_rms;
            else if (ff->type() == Mpw)
                str = mkw_pw;
            else if (ff->type() == Mrft)
                str = mkw_rt;
            if (ro_print_flag > 1) {
                if (ff->error())
                    sprintf(buf, "    %s: (error occurred)\n", str);
                else
                    sprintf(buf, "    %s: %g\n", str, ff->val());
            }
            else {
                if (ff->error())
                    sprintf(buf, "%s %s: (error occurred)\n", ro_result, str);
                else
                    sprintf(buf, "%s %s: %g\n", ro_result, str, ff->val());
            }
            lstr.add(buf);
        }
    }
    else if (ro_start.found_flag()) {
        if (ro_print_flag > 1) {
            if (ro_cktptr && ro_cktptr->runplot() && ro_cktptr->runplot()->scale()) {
                const char *zz = ro_cktptr->runplot()->scale()->name();
                if (zz && *zz) {
                    sprintf(buf, " %s\n", zz);
                    lstr.add(buf);
                }
            }
            sprintf(buf, "    start: %g\n", ro_start.found());
            lstr.add(buf);
        }
        for (sMfunc *ff = ro_finds; ff; ff = ff->next()) {
            if (ro_print_flag > 1) {
                if (ff->error())
                    sprintf(buf, " %s = (error occurred)\n", ff->expr());
                else
                    sprintf(buf, " %s = %g\n", ff->expr(), ff->val());
            }
            else {
                if (ff->error())
                    sprintf(buf, "%s %s = (error occurred)\n", ro_result,
                        ff->expr());
                else
                    sprintf(buf, "%s %s = %g\n", ro_result, ff->expr(), ff->val());
            }
            lstr.add(buf);
        }
    }
    char *str = lstr.string_trim();
    lstr.clear();
    return (str);
}


// Add a measurement to the appropriate list.  This is a private function
// called during the parse.
//
void
sRunopMeas::addMeas(Mfunc mtype, const char *expr)
{
    sMfunc *m = new sMfunc(mtype, expr);
    if (mtype == Mfind) {
        if (!ro_finds)
            ro_finds = m;
        else {
            sMfunc *mm = ro_finds;
            while (mm->next())
                mm = mm->next();
            mm->set_next(m);
        }
    }
    else {
        if (!ro_funcs)
            ro_funcs = m;
        else {
            sMfunc *mm = ro_funcs;
            while (mm->next())
                mm = mm->next();
            mm->set_next(m);
        }
    }
}


// Return the interpolated end values of dv.  If end is false, look at
// the start side, where the interval starts ahead of the index point.
// Otherwise, the value is after the index point.
//
double
sRunopMeas::endval(sDataVec *dv, sDataVec *xs, bool final)
{
    if (!final) {
        int i = ro_start.indx();
        if (ro_start.found() != xs->realval(i) && i > 0) {
            double y0 = value(dv, ro_start.indx());
            double y1 = value(dv, ro_start.indx() - 1);
            return (y0 + (y1 - y0)*(ro_start.found() - xs->realval(i))/
                (xs->realval(i-1) - xs->realval(i)));
        }
        else
            return (value(dv, ro_start.indx()));
    }
    else {
        int i = ro_end.indx();
        if (ro_end.found() != xs->realval(i) && i+1 < dv->length()) {
            double y0 = value(dv, ro_end.indx());
            double y1 = value(dv, ro_end.indx() + 1);
            return (y0 + (y1 - y0)*(ro_end.found() - xs->realval(i))/
                (xs->realval(i+1) - xs->realval(i)));
        }
        else
            return (value(dv, ro_end.indx()));
    }
}


// Find the average of dv over the interval.  We use trapezoid
// integration and divide by the scale interval.
//
double
sRunopMeas::findavg(sDataVec *dv, sDataVec *xs)
{
    double sum = 0.0;
    double d, delt;
    // compute the sum over the scale intervals
    int i;
    for (i = ro_start.indx()+1; i <= ro_end.indx(); i++) {
        delt = xs->realval(i) - xs->realval(i-1);
        sum += 0.5*delt*(dv->realval(i-1) + dv->realval(i));
    }

    // ro_start.found() is ahead of the start index (between i-1 and i)
    delt = xs->realval(ro_start.indx()) - ro_start.found();
    if (delt != 0) {
        i = ro_start.indx();
        d = dv->realval(i) + (dv->realval(i-1) - dv->realval(i))*
            (ro_start.found() - xs->realval(ro_start.indx()))/
            (xs->realval(i-1) - xs->realval(i));
        sum += 0.5*delt*(d + dv->realval(i));
    }

    // ro_end.found() is ahead if the end index (between i-1 and i)
    delt = ro_end.found() - xs->realval(ro_end.indx());
    if (delt != 0.0) {
        i = ro_end.indx();
        d = dv->realval(i) + (dv->realval(i+1) - dv->realval(i))*
            (ro_end.found() - xs->realval(ro_end.indx()))/
            (xs->realval(i+1) - xs->realval(i));
        sum += 0.5*delt*(d + dv->realval(i));
    }

    double dt = ro_end.found() - ro_start.found();
    if (dt != 0.0)
        sum /= dt;
    return (sum);
}


// Find the rms of dv over the interval.  We use trapezoid
// integration of the magnitudes.
//
double
sRunopMeas::findrms(sDataVec *dv, sDataVec *xs)
{
    double sum = 0.0;
    double d, delt;
    // compute the sum over the scale intervals
    int i;
    for (i = ro_start.indx()+1; i <= ro_end.indx(); i++) {
        delt = xs->realval(i) - xs->realval(i-1);
        sum += 0.5*delt*(dv->realval(i-1)*dv->realval(i-1) +
            dv->realval(i)*dv->realval(i));
    }

    // ro_start.found() is ahead of the start index
    delt = xs->realval(ro_start.indx()) - ro_start.found();
    if (delt != 0.0) {
        i = ro_start.indx();
        d = dv->realval(i) + (dv->realval(i-1) - dv->realval(i))*
            (ro_start.found() - xs->realval(ro_start.indx()))/
            (xs->realval(i-1) - xs->realval(i));
        sum += 0.5*delt*(d*d + dv->realval(i)*dv->realval(i));
    }

    // ro_end.found() is behind the end index
    delt = ro_end.found() - xs->realval(ro_end.indx());
    if (delt != 0.0) {
        i = ro_end.indx();
        d = dv->realval(i) + (dv->realval(i+1) - dv->realval(i))*
            (ro_end.found() - xs->realval(ro_end.indx()))/
            (xs->realval(i+1) - xs->realval(i));
        sum += 0.5*delt*(d*d + dv->realval(i)*dv->realval(i));
    }

    double dt = ro_end.found() - ro_start.found();
    if (dt != 0.0)
        sum /= dt;
    return (sqrt(fabs(sum)));
}


// Find the fwhm of a pulse assumed to be contained in the interval.
//
double
sRunopMeas::findpw(sDataVec *dv, sDataVec *xs)
{
    // find the max/min
    double mx = dv->realval(ro_start.indx());
    double mn = mx;
    int imx = -1;
    int imn = -1;
    for (int i = ro_start.indx()+1; i <= ro_end.indx(); i++) {
        if (dv->realval(i) > mx) {
            mx = dv->realval(i);
            imx = i;
        }
        if (dv->realval(i) < mn) {
            mn = dv->realval(i);
            imn = i;
        }
    }
    double ds = endval(dv, xs, false);
    double de = endval(dv, xs, true);
    double mid;
    int imid;
    if (mx - SPMAX(ds, de) > SPMIN(ds, de) - mn) {
        mid = 0.5*(mx + SPMAX(ds, de));
        imid = imx;
    }
    else {
        mid = 0.5*(mn + SPMIN(ds, de));
        imid = imn;
    }

    int ibeg = -1, iend = -1;
    for (int i = ro_start.indx() + 1; i <= ro_end.indx(); i++) {
        if ((dv->realval(i-1) < mid && dv->realval(i) >= mid) ||
                (dv->realval(i-1) > mid && dv->realval(i) <= mid)) {
            if (ibeg >= 0)
                iend = i;
            else
                ibeg = i;
        }
        if (ibeg >=0 && iend >= 0)
            break;
    }
    if (ibeg >= 0 && iend >= 0 && ibeg < imid && iend > imid) {
        double x0 = xs->realval(ibeg-1);
        double x1 = xs->realval(ibeg);
        double y0 = dv->realval(ibeg-1);
        double y1 = dv->realval(ibeg);
        double tbeg = x0 + (x1 - x0)*(mid - y0)/(y1 - y0);
        x0 = xs->realval(iend-1);
        x1 = xs->realval(iend);
        y0 = dv->realval(iend-1);
        y1 = dv->realval(iend);
        double tend = x0 + (x1 - x0)*(mid - y0)/(y1 - y0);
        return(tend - tbeg);
    }
    return (0.0);
}


// Find the 10-90% rise or fall time of an edge contained in the
// interval.
//
double
sRunopMeas::findrft(sDataVec *dv, sDataVec *xs)
{
    double vstart = endval(dv, xs, false);
    double vend = endval(dv, xs, true);
    double th1 = vstart + 0.1*(vend - vstart);
    double th2 = vstart + 0.9*(vend - vstart);

    int ibeg = -1, iend = -1;
    for (int i = ro_start.indx() + 1; i <= ro_end.indx(); i++) {
        if ((dv->realval(i-1) < th1 && dv->realval(i) >= th1) ||
                (dv->realval(i-1) > th1 && dv->realval(i) <= th1) ||
                (dv->realval(i-1) < th2 && dv->realval(i) >= th2) ||
                (dv->realval(i-1) > th2 && dv->realval(i) <= th2)) {
            if (ibeg >= 0)
                iend = i;
            else
                ibeg = i;
        }
        if (ibeg >=0 && iend >= 0)
            break;
    }
    if (ibeg >= 0 && iend >= 0 && iend > ibeg) {
        double x0 = xs->realval(ibeg-1);
        double x1 = xs->realval(ibeg);
        double y0 = dv->realval(ibeg-1);
        double y1 = dv->realval(ibeg);
        double tbeg = x0 + (x1 - x0)*(th1 - y0)/(y1 - y0);
        x0 = xs->realval(iend-1);
        x1 = xs->realval(iend);
        y0 = dv->realval(iend-1);
        y1 = dv->realval(iend);
        double tend = x0 + (x1 - x0)*(th2 - y0)/(y1 - y0);
        return(tend - tbeg);
    }
    return (0.0);
}

