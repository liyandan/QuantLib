#include <ql/experimental/credit/gaussianlhplossmodel.hpp>

#include <boost/make_shared.hpp>

namespace QuantLib {

    GaussianLHPLossModel::GaussianLHPLossModel(
            const Handle<Quote>& correlQuote,
            const std::vector<Handle<RecoveryRateQuote> >& quotes)
        : correl_(correlQuote),
          rrQuotes_(quotes), 
          sqrt1minuscorrel_(std::sqrt(1.-correlQuote->value())),
          biphi_(-sqrt(correlQuote->value())),
          beta_(sqrt(correlQuote->value())),
          LatentModel<GaussianCopulaPolicy>(sqrt(correlQuote->value()),
            quotes.size())
        {
            registerWith(correl_);
            for(Size i=0; i<quotes.size(); i++)
                registerWith(quotes[i]);
            this->update();//?////////////////////////////////////////////////////////////////old mechanics
        }

    GaussianLHPLossModel::GaussianLHPLossModel(
            Real correlation,
            const std::vector<Real>& recoveries)
        :
        correl_(Handle<Quote>(boost::make_shared<SimpleQuote>(correlation))),
          sqrt1minuscorrel_(std::sqrt(1.-correlation)),
          biphi_(-sqrt(correlation)),
          beta_(sqrt(correlation)),
          LatentModel<GaussianCopulaPolicy>(sqrt(correlation),
            recoveries.size())
        {
            for(Size i=0; i<recoveries.size(); i++)
                rrQuotes_.push_back(Handle<RecoveryRateQuote>(
                boost::make_shared<RecoveryRateQuote>(recoveries[i])));
            this->update();//?//?////////////////////////////////////////////////////////////////old mechanics
        }

        GaussianLHPLossModel::GaussianLHPLossModel(
            const Handle<Quote>& correlQuote,
            const std::vector<Real>& recoveries)
        : correl_(correlQuote),
          sqrt1minuscorrel_(std::sqrt(1.-correlQuote->value())),
          biphi_(-sqrt(correlQuote->value())),
          beta_(sqrt(correlQuote->value())),
          LatentModel<GaussianCopulaPolicy>(sqrt(correlQuote->value()),
            recoveries.size())
        {
            registerWith(correl_);
            for(Size i=0; i<recoveries.size(); i++)
                rrQuotes_.push_back(Handle<RecoveryRateQuote>(
                boost::make_shared<RecoveryRateQuote>(recoveries[i])));
            this->update();//?/////////////////////////////////////////////////////////////old mechanics
        }


        Real GaussianLHPLossModel::expectedTrancheLossImpl(
            Real remainingNot, // << at the given date 'd'
            Real prob, // << at the given date 'd'
            Real averageRR, // << at the given date 'd'
            Real attachLimit, Real detachLimit) const 
        {

            if (attachLimit >= detachLimit) return 0.;// or is it an error?
            // expected remaining notional:
            if (remainingNot == 0.) return 0.;

            const Real one = 1.0 - 1.0e-12;  // FIXME DUE TO THE INV CUMUL AT 1
            const Real k1 = std::min(one, attachLimit / 
                (1.0 - averageRR)) + QL_EPSILON;
            const Real k2 = std::min(one, detachLimit / 
                (1.0 - averageRR)) + QL_EPSILON;

            if (prob > 0) {
                const Real ip = InverseCumulativeNormal::standard_value(prob);

                if(k1>0.)
                    return remainingNot * (1.-averageRR) * 
                      (biphi_(-InverseCumulativeNormal::standard_value(k1), ip)-
                      biphi_(-InverseCumulativeNormal::standard_value(k2), ip));
                else
                    return remainingNot * (1.-averageRR) * (prob - 
                      biphi_(-InverseCumulativeNormal::standard_value(k2), ip));

                const Real invFlightK1 = 
                    (ip-sqrt1minuscorrel_ * 
                        InverseCumulativeNormal::standard_value(k1))/beta_;
                const Real invFlightK2 = (ip-sqrt1minuscorrel_*
                    InverseCumulativeNormal::standard_value(k2))/beta_;

                return remainingNot * (detachLimit * phi_(invFlightK2) 
                    - attachLimit * phi_(invFlightK1) + (1.-averageRR) * 
                    (biphi_(ip, -invFlightK2) - biphi_(ip, -invFlightK1)) );
            }
            else return 0.0;
        }

        Real GaussianLHPLossModel::probOverLoss(const Date& d,
            Real remainingLossFraction) const {
                // check d is not before todays date

            QL_REQUIRE(remainingLossFraction >=0., "Incorrect loss fraction.");
            QL_REQUIRE(remainingLossFraction <=1., "Incorrect loss fraction.");

            // live unerlying portfolio loss fraction (remaining portf fraction)

            const Real remainingBasktNot = basket_->remainingNotional(d);
            const Real attach = 
                std::min(remainingAttachAmount_ / remainingBasktNot, 1.);
            const Real detach = 
                std::min(remainingDetachAmount_ / remainingBasktNot, 1.);

            Real portfFract = 
                attach + remainingLossFraction * (detach - attach);

            Real averageRR = averageRecovery(d);
            Real maxAttLossFract = (1.-averageRR); // GLOBAL at CLASS SCOPE?????????????? updateable on observability triggers
            if(portfFract > maxAttLossFract) return 0.;

            // for non-equity losses add the probability jump at zero tranche 
            //   losses (since this method returns prob of losing more or 
            //   equal to)
            if(portfFract <= QL_EPSILON) return 1.;

            Probability prob = averageProb(d);

            Real ip = InverseCumulativeNormal::standard_value(prob);
            Real invFlightK = (ip-sqrt1minuscorrel_*
                InverseCumulativeNormal::standard_value(portfFract
                    /(1.-averageRR)))/beta_;

            return  phi_(invFlightK);//probOver
        }

        Real GaussianLHPLossModel::expectedShortfall(const Date& d, 
            Probability perctl) const 
        {
            // loss as a fraction of the live portfolio
            Real ptflLossPerc = percentilePortfolioLossFraction(d, perctl);

            const Real remainingNot = basket_->remainingNotional(d);
            const Real attach = 
                std::min(remainingAttachAmount_ / remainingNot, 1.);
            const Real detach = 
                std::min(remainingDetachAmount_ / remainingNot, 1.);

            if(ptflLossPerc >= detach-QL_EPSILON) 
                return remainingNot * (detach-attach);//equivalent

            Real maxLossLevel = std::max(attach, ptflLossPerc);
            Probability prob = averageProb(d);
            Real averageRR = averageRecovery(d);

            Real valA = expectedTrancheLossImpl(remainingNot, prob, 
                averageRR, maxLossLevel, detach);
            Real valB = // probOverLoss(d, maxLossLevel);//in live tranche units
            // from fraction of basket notional to fraction of tranche notional
                probOverLoss(d, std::min(std::max((maxLossLevel - attach)
                /(detach - attach), 0.), 1.));
            return ( valA + (maxLossLevel - attach) * remainingNot * valB )
                / (1.-perctl);
        }

        Real GaussianLHPLossModel::percentilePortfolioLossFraction(
            const Date& d, Real perctl) const 
        {
            QL_REQUIRE(perctl >= 0. && perctl <=1., 
                "Percentile argument out of bounds.");

            if(perctl==0.) return 0.;// portfl == attach
            if(perctl==1.) perctl = 1. - QL_EPSILON; // portfl == detach

            return (1.-averageRecovery(d)) * 
                phi_( ( InverseCumulativeNormal::standard_value(averageProb(d))
                    + beta_ * InverseCumulativeNormal::standard_value(perctl) )
                        /sqrt1minuscorrel_);
        }

}
