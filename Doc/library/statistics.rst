:mod:`statistics` --- Mathematical statistics functions
=======================================================

.. module:: statistics
   :synopsis: mathematical statistics functions

.. moduleauthor:: Steven D'Aprano <steve+python@pearwood.info>
.. sectionauthor:: Steven D'Aprano <steve+python@pearwood.info>

.. versionadded:: 3.4

**Source code:** :source:`Lib/statistics.py`

.. testsetup:: *

   from statistics import *
   __name__ = '<doctest>'

--------------

This module provides functions for calculating mathematical statistics of
numeric (:class:`Real`-valued) data.

.. note::

   Unless explicitly noted otherwise, these functions support :class:`int`,
   :class:`float`, :class:`decimal.Decimal` and :class:`fractions.Fraction`.
   Behaviour with other types (whether in the numeric tower or not) is
   currently unsupported.  Mixed types are also undefined and
   implementation-dependent.  If your input data consists of mixed types,
   you may be able to use :func:`map` to ensure a consistent result, e.g.
   ``map(float, input_data)``.

Averages and measures of central location
-----------------------------------------

These functions calculate an average or typical value from a population
or sample.

=======================  =============================================
:func:`mean`             Arithmetic mean ("average") of data.
:func:`fmean`            Fast, floating point arithmetic mean.
:func:`harmonic_mean`    Harmonic mean of data.
:func:`median`           Median (middle value) of data.
:func:`median_low`       Low median of data.
:func:`median_high`      High median of data.
:func:`median_grouped`   Median, or 50th percentile, of grouped data.
:func:`mode`             Mode (most common value) of discrete data.
=======================  =============================================

Measures of spread
------------------

These functions calculate a measure of how much the population or sample
tends to deviate from the typical or average values.

=======================  =============================================
:func:`pstdev`           Population standard deviation of data.
:func:`pvariance`        Population variance of data.
:func:`stdev`            Sample standard deviation of data.
:func:`variance`         Sample variance of data.
=======================  =============================================


Function details
----------------

Note: The functions do not require the data given to them to be sorted.
However, for reading convenience, most of the examples show sorted sequences.

.. function:: mean(data)

   Return the sample arithmetic mean of *data* which can be a sequence or iterator.

   The arithmetic mean is the sum of the data divided by the number of data
   points.  It is commonly called "the average", although it is only one of many
   different mathematical averages.  It is a measure of the central location of
   the data.

   If *data* is empty, :exc:`StatisticsError` will be raised.

   Some examples of use:

   .. doctest::

      >>> mean([1, 2, 3, 4, 4])
      2.8
      >>> mean([-1.0, 2.5, 3.25, 5.75])
      2.625

      >>> from fractions import Fraction as F
      >>> mean([F(3, 7), F(1, 21), F(5, 3), F(1, 3)])
      Fraction(13, 21)

      >>> from decimal import Decimal as D
      >>> mean([D("0.5"), D("0.75"), D("0.625"), D("0.375")])
      Decimal('0.5625')

   .. note::

      The mean is strongly affected by outliers and is not a robust estimator
      for central location: the mean is not necessarily a typical example of the
      data points.  For more robust, although less efficient, measures of
      central location, see :func:`median` and :func:`mode`.  (In this case,
      "efficient" refers to statistical efficiency rather than computational
      efficiency.)

      The sample mean gives an unbiased estimate of the true population mean,
      which means that, taken on average over all the possible samples,
      ``mean(sample)`` converges on the true mean of the entire population.  If
      *data* represents the entire population rather than a sample, then
      ``mean(data)`` is equivalent to calculating the true population mean μ.


.. function:: fmean(data)

   Convert *data* to floats and compute the arithmetic mean.

   This runs faster than the :func:`mean` function and it always returns a
   :class:`float`.  The result is highly accurate but not as perfect as
   :func:`mean`.  If the input dataset is empty, raises a
   :exc:`StatisticsError`.

   .. doctest::

      >>> fmean([3.5, 4.0, 5.25])
      4.25

   .. versionadded:: 3.8


.. function:: harmonic_mean(data)

   Return the harmonic mean of *data*, a sequence or iterator of
   real-valued numbers.

   The harmonic mean, sometimes called the subcontrary mean, is the
   reciprocal of the arithmetic :func:`mean` of the reciprocals of the
   data. For example, the harmonic mean of three values *a*, *b* and *c*
   will be equivalent to ``3/(1/a + 1/b + 1/c)``.

   The harmonic mean is a type of average, a measure of the central
   location of the data.  It is often appropriate when averaging quantities
   which are rates or ratios, for example speeds. For example:

   Suppose an investor purchases an equal value of shares in each of
   three companies, with P/E (price/earning) ratios of 2.5, 3 and 10.
   What is the average P/E ratio for the investor's portfolio?

   .. doctest::

      >>> harmonic_mean([2.5, 3, 10])  # For an equal investment portfolio.
      3.6

   Using the arithmetic mean would give an average of about 5.167, which
   is too high.

   :exc:`StatisticsError` is raised if *data* is empty, or any element
   is less than zero.

   .. versionadded:: 3.6


.. function:: median(data)

   Return the median (middle value) of numeric data, using the common "mean of
   middle two" method.  If *data* is empty, :exc:`StatisticsError` is raised.
   *data* can be a sequence or iterator.

   The median is a robust measure of central location, and is less affected by
   the presence of outliers in your data.  When the number of data points is
   odd, the middle data point is returned:

   .. doctest::

      >>> median([1, 3, 5])
      3

   When the number of data points is even, the median is interpolated by taking
   the average of the two middle values:

   .. doctest::

      >>> median([1, 3, 5, 7])
      4.0

   This is suited for when your data is discrete, and you don't mind that the
   median may not be an actual data point.

   If your data is ordinal (supports order operations) but not numeric (doesn't
   support addition), you should use :func:`median_low` or :func:`median_high`
   instead.

   .. seealso:: :func:`median_low`, :func:`median_high`, :func:`median_grouped`


.. function:: median_low(data)

   Return the low median of numeric data.  If *data* is empty,
   :exc:`StatisticsError` is raised.  *data* can be a sequence or iterator.

   The low median is always a member of the data set.  When the number of data
   points is odd, the middle value is returned.  When it is even, the smaller of
   the two middle values is returned.

   .. doctest::

      >>> median_low([1, 3, 5])
      3
      >>> median_low([1, 3, 5, 7])
      3

   Use the low median when your data are discrete and you prefer the median to
   be an actual data point rather than interpolated.


.. function:: median_high(data)

   Return the high median of data.  If *data* is empty, :exc:`StatisticsError`
   is raised.  *data* can be a sequence or iterator.

   The high median is always a member of the data set.  When the number of data
   points is odd, the middle value is returned.  When it is even, the larger of
   the two middle values is returned.

   .. doctest::

      >>> median_high([1, 3, 5])
      3
      >>> median_high([1, 3, 5, 7])
      5

   Use the high median when your data are discrete and you prefer the median to
   be an actual data point rather than interpolated.


.. function:: median_grouped(data, interval=1)

   Return the median of grouped continuous data, calculated as the 50th
   percentile, using interpolation.  If *data* is empty, :exc:`StatisticsError`
   is raised.  *data* can be a sequence or iterator.

   .. doctest::

      >>> median_grouped([52, 52, 53, 54])
      52.5

   In the following example, the data are rounded, so that each value represents
   the midpoint of data classes, e.g. 1 is the midpoint of the class 0.5--1.5, 2
   is the midpoint of 1.5--2.5, 3 is the midpoint of 2.5--3.5, etc.  With the data
   given, the middle value falls somewhere in the class 3.5--4.5, and
   interpolation is used to estimate it:

   .. doctest::

      >>> median_grouped([1, 2, 2, 3, 4, 4, 4, 4, 4, 5])
      3.7

   Optional argument *interval* represents the class interval, and defaults
   to 1.  Changing the class interval naturally will change the interpolation:

   .. doctest::

      >>> median_grouped([1, 3, 3, 5, 7], interval=1)
      3.25
      >>> median_grouped([1, 3, 3, 5, 7], interval=2)
      3.5

   This function does not check whether the data points are at least
   *interval* apart.

   .. impl-detail::

      Under some circumstances, :func:`median_grouped` may coerce data points to
      floats.  This behaviour is likely to change in the future.

   .. seealso::

      * "Statistics for the Behavioral Sciences", Frederick J Gravetter and
        Larry B Wallnau (8th Edition).

      * The `SSMEDIAN
        <https://help.gnome.org/users/gnumeric/stable/gnumeric.html#gnumeric-function-SSMEDIAN>`_
        function in the Gnome Gnumeric spreadsheet, including `this discussion
        <https://mail.gnome.org/archives/gnumeric-list/2011-April/msg00018.html>`_.


.. function:: mode(data)

   Return the most common data point from discrete or nominal *data*.  The mode
   (when it exists) is the most typical value, and is a robust measure of
   central location.

   If *data* is empty, or if there is not exactly one most common value,
   :exc:`StatisticsError` is raised.

   ``mode`` assumes discrete data, and returns a single value. This is the
   standard treatment of the mode as commonly taught in schools:

   .. doctest::

      >>> mode([1, 1, 2, 3, 3, 3, 3, 4])
      3

   The mode is unique in that it is the only statistic which also applies
   to nominal (non-numeric) data:

   .. doctest::

      >>> mode(["red", "blue", "blue", "red", "green", "red", "red"])
      'red'


.. function:: pstdev(data, mu=None)

   Return the population standard deviation (the square root of the population
   variance).  See :func:`pvariance` for arguments and other details.

   .. doctest::

      >>> pstdev([1.5, 2.5, 2.5, 2.75, 3.25, 4.75])
      0.986893273527251


.. function:: pvariance(data, mu=None)

   Return the population variance of *data*, a non-empty iterable of real-valued
   numbers.  Variance, or second moment about the mean, is a measure of the
   variability (spread or dispersion) of data.  A large variance indicates that
   the data is spread out; a small variance indicates it is clustered closely
   around the mean.

   If the optional second argument *mu* is given, it should be the mean of
   *data*.  If it is missing or ``None`` (the default), the mean is
   automatically calculated.

   Use this function to calculate the variance from the entire population.  To
   estimate the variance from a sample, the :func:`variance` function is usually
   a better choice.

   Raises :exc:`StatisticsError` if *data* is empty.

   Examples:

   .. doctest::

      >>> data = [0.0, 0.25, 0.25, 1.25, 1.5, 1.75, 2.75, 3.25]
      >>> pvariance(data)
      1.25

   If you have already calculated the mean of your data, you can pass it as the
   optional second argument *mu* to avoid recalculation:

   .. doctest::

      >>> mu = mean(data)
      >>> pvariance(data, mu)
      1.25

   This function does not attempt to verify that you have passed the actual mean
   as *mu*.  Using arbitrary values for *mu* may lead to invalid or impossible
   results.

   Decimals and Fractions are supported:

   .. doctest::

      >>> from decimal import Decimal as D
      >>> pvariance([D("27.5"), D("30.25"), D("30.25"), D("34.5"), D("41.75")])
      Decimal('24.815')

      >>> from fractions import Fraction as F
      >>> pvariance([F(1, 4), F(5, 4), F(1, 2)])
      Fraction(13, 72)

   .. note::

      When called with the entire population, this gives the population variance
      σ².  When called on a sample instead, this is the biased sample variance
      s², also known as variance with N degrees of freedom.

      If you somehow know the true population mean μ, you may use this function
      to calculate the variance of a sample, giving the known population mean as
      the second argument.  Provided the data points are representative
      (e.g. independent and identically distributed), the result will be an
      unbiased estimate of the population variance.


.. function:: stdev(data, xbar=None)

   Return the sample standard deviation (the square root of the sample
   variance).  See :func:`variance` for arguments and other details.

   .. doctest::

      >>> stdev([1.5, 2.5, 2.5, 2.75, 3.25, 4.75])
      1.0810874155219827


.. function:: variance(data, xbar=None)

   Return the sample variance of *data*, an iterable of at least two real-valued
   numbers.  Variance, or second moment about the mean, is a measure of the
   variability (spread or dispersion) of data.  A large variance indicates that
   the data is spread out; a small variance indicates it is clustered closely
   around the mean.

   If the optional second argument *xbar* is given, it should be the mean of
   *data*.  If it is missing or ``None`` (the default), the mean is
   automatically calculated.

   Use this function when your data is a sample from a population. To calculate
   the variance from the entire population, see :func:`pvariance`.

   Raises :exc:`StatisticsError` if *data* has fewer than two values.

   Examples:

   .. doctest::

      >>> data = [2.75, 1.75, 1.25, 0.25, 0.5, 1.25, 3.5]
      >>> variance(data)
      1.3720238095238095

   If you have already calculated the mean of your data, you can pass it as the
   optional second argument *xbar* to avoid recalculation:

   .. doctest::

      >>> m = mean(data)
      >>> variance(data, m)
      1.3720238095238095

   This function does not attempt to verify that you have passed the actual mean
   as *xbar*.  Using arbitrary values for *xbar* can lead to invalid or
   impossible results.

   Decimal and Fraction values are supported:

   .. doctest::

      >>> from decimal import Decimal as D
      >>> variance([D("27.5"), D("30.25"), D("30.25"), D("34.5"), D("41.75")])
      Decimal('31.01875')

      >>> from fractions import Fraction as F
      >>> variance([F(1, 6), F(1, 2), F(5, 3)])
      Fraction(67, 108)

   .. note::

      This is the sample variance s² with Bessel's correction, also known as
      variance with N-1 degrees of freedom.  Provided that the data points are
      representative (e.g. independent and identically distributed), the result
      should be an unbiased estimate of the true population variance.

      If you somehow know the actual population mean μ you should pass it to the
      :func:`pvariance` function as the *mu* parameter to get the variance of a
      sample.

Exceptions
----------

A single exception is defined:

.. exception:: StatisticsError

   Subclass of :exc:`ValueError` for statistics-related exceptions.


:class:`NormalDist` objects
===========================

A :class:`NormalDist` is a a composite class that treats the mean and standard
deviation of data measurements as a single entity.  It is a tool for creating
and manipulating normal distributions of a random variable.

Normal distributions arise from the `Central Limit Theorem
<https://en.wikipedia.org/wiki/Central_limit_theorem>`_ and have a wide range
of applications in statistics, including simulations and hypothesis testing.

.. class:: NormalDist(mu=0.0, sigma=1.0)

    Returns a new *NormalDist* object where *mu* represents the `arithmetic
    mean <https://en.wikipedia.org/wiki/Arithmetic_mean>`_ of data and *sigma*
    represents the `standard deviation
    <https://en.wikipedia.org/wiki/Standard_deviation>`_ of the data.

    If *sigma* is negative, raises :exc:`StatisticsError`.

    .. attribute:: mean

       A read-only property representing the `arithmetic mean
       <https://en.wikipedia.org/wiki/Arithmetic_mean>`_ of a normal
       distribution.

    .. attribute:: stdev

       A read-only property representing the `standard deviation
       <https://en.wikipedia.org/wiki/Standard_deviation>`_ of a normal
       distribution.

    .. attribute:: variance

       A read-only property representing the `variance
       <https://en.wikipedia.org/wiki/Variance>`_ of a normal
       distribution. Equal to the square of the standard deviation.

    .. classmethod:: NormalDist.from_samples(data)

       Class method that makes a normal distribution instance
       from sample data.  The *data* can be any :term:`iterable`
       and should consist of values that can be converted to type
       :class:`float`.

       If *data* does not contain at least two elements, raises
       :exc:`StatisticsError` because it takes at least one point to estimate
       a central value and at least two points to estimate dispersion.

    .. method:: NormalDist.samples(n, seed=None)

       Generates *n* random samples for a given mean and standard deviation.
       Returns a :class:`list` of :class:`float` values.

       If *seed* is given, creates a new instance of the underlying random
       number generator.  This is useful for creating reproducible results,
       even in a multi-threading context.

    .. method:: NormalDist.pdf(x)

       Using a `probability density function (pdf)
       <https://en.wikipedia.org/wiki/Probability_density_function>`_,
       compute the relative likelihood that a random sample *X* will be near
       the given value *x*.  Mathematically, it is the ratio ``P(x <= X <
       x+dx) / dx``.

       Note the relative likelihood of *x* can be greater than `1.0`.  The
       probability for a specific point on a continuous distribution is `0.0`,
       so the :func:`pdf` is used instead.  It gives the probability of a
       sample occurring in a narrow range around *x* and then dividing that
       probability by the width of the range (hence the word "density").

    .. method:: NormalDist.cdf(x)

       Using a `cumulative distribution function (cdf)
       <https://en.wikipedia.org/wiki/Cumulative_distribution_function>`_,
       compute the probability that a random sample *X* will be less than or
       equal to *x*.  Mathematically, it is written ``P(X <= x)``.

    Instances of :class:`NormalDist` support addition, subtraction,
    multiplication and division by a constant.  These operations
    are used for translation and scaling.  For example:

    .. doctest::

        >>> temperature_february = NormalDist(5, 2.5)             # Celsius
        >>> temperature_february * (9/5) + 32                     # Fahrenheit
        NormalDist(mu=41.0, sigma=4.5)

    Dividing a constant by an instance of :class:`NormalDist` is not supported.

    Since normal distributions arise from additive effects of independent
    variables, it is possible to `add and subtract two normally distributed
    random variables
    <https://en.wikipedia.org/wiki/Sum_of_normally_distributed_random_variables>`_
    represented as instances of :class:`NormalDist`.  For example:

    .. doctest::

        >>> birth_weights = NormalDist.from_samples([2.5, 3.1, 2.1, 2.4, 2.7, 3.5])
        >>> drug_effects = NormalDist(0.4, 0.15)
        >>> combined = birth_weights + drug_effects
        >>> f'mean: {combined.mean :.1f}   standard deviation: {combined.stdev :.1f}'
        'mean: 3.1   standard deviation: 0.5'

    .. versionadded:: 3.8


:class:`NormalDist` Examples and Recipes
----------------------------------------

A :class:`NormalDist` readily solves classic probability problems.

For example, given `historical data for SAT exams
<https://blog.prepscholar.com/sat-standard-deviation>`_ showing that scores
are normally distributed with a mean of 1060 and standard deviation of 192,
determine the percentage of students with scores between 1100 and 1200:

.. doctest::

    >>> sat = NormalDist(1060, 195)
    >>> fraction = sat.cdf(1200) - sat.cdf(1100)
    >>> f'{fraction * 100 :.1f}% score between 1100 and 1200'
    '18.2% score between 1100 and 1200'

To estimate the distribution for a model than isn't easy to solve
analytically, :class:`NormalDist` can generate input samples for a `Monte
Carlo simulation <https://en.wikipedia.org/wiki/Monte_Carlo_method>`_ of the
model:

.. doctest::

    >>> n = 100_000
    >>> X = NormalDist(350, 15).samples(n)
    >>> Y = NormalDist(47, 17).samples(n)
    >>> Z = NormalDist(62, 6).samples(n)
    >>> model_simulation = [x * y / z for x, y, z in zip(X, Y, Z)]
    >>> NormalDist.from_samples(model_simulation)           # doctest: +SKIP
    NormalDist(mu=267.6516398754636, sigma=101.357284306067)

Normal distributions commonly arise in machine learning problems.

Wikipedia has a `nice example with a Naive Bayesian Classifier
<https://en.wikipedia.org/wiki/Naive_Bayes_classifier>`_.  The challenge
is to guess a person's gender from measurements of normally distributed
features including height, weight, and foot size.

The `prior probability <https://en.wikipedia.org/wiki/Prior_probability>`_ of
being male or female is 50%:

.. doctest::

    >>> prior_male = 0.5
    >>> prior_female = 0.5

We also have a training dataset with measurements for eight people.  These
measurements are assumed to be normally distributed, so we summarize the data
with :class:`NormalDist`:

.. doctest::

    >>> height_male = NormalDist.from_samples([6, 5.92, 5.58, 5.92])
    >>> height_female = NormalDist.from_samples([5, 5.5, 5.42, 5.75])
    >>> weight_male = NormalDist.from_samples([180, 190, 170, 165])
    >>> weight_female = NormalDist.from_samples([100, 150, 130, 150])
    >>> foot_size_male = NormalDist.from_samples([12, 11, 12, 10])
    >>> foot_size_female = NormalDist.from_samples([6, 8, 7, 9])

We observe a new person whose feature measurements are known but whose gender
is unknown:

.. doctest::

    >>> ht = 6.0        # height
    >>> wt = 130        # weight
    >>> fs = 8          # foot size

The posterior is the product of the prior times each likelihood of a
feature measurement given the gender:

.. doctest::

   >>> posterior_male = (prior_male * height_male.pdf(ht) *
   ...                   weight_male.pdf(wt) * foot_size_male.pdf(fs))

   >>> posterior_female = (prior_female * height_female.pdf(ht) *
   ...                     weight_female.pdf(wt) * foot_size_female.pdf(fs))

The final prediction is awarded to the largest posterior -- this is known as
the `maximum a posteriori
<https://en.wikipedia.org/wiki/Maximum_a_posteriori_estimation>`_ or MAP:

.. doctest::

  >>> 'male' if posterior_male > posterior_female else 'female'
  'female'


..
   # This modelines must appear within the last ten lines of the file.
   kate: indent-width 3; remove-trailing-space on; replace-tabs on; encoding utf-8;
